/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The host interface: SMBus channel B as an I2C slave at 0x76 (RESLADR2B)
 * on the SoC's I2C6, carrying CIX's ChromeOS-EC style host commands
 * (protocol 3; the command and length fields big-endian).  UEFI's
 * Ite5570EcLib and Linux's cros-ec (device tree: google,cros-ec-i2c with
 * cix,cix-ec-* children) use it.
 *
 * Request bytes:  0xDA, version 3, checksum, command (2), command version,
 *                 reserved, length (2), parameters.
 * Response bytes: result, 10 + length, version 3, checksum, result (2),
 *                 length (2), reserved (2), data.
 *
 * As the vendor firmware: the parameters and the response data live in
 * buffers that persist between commands (a few handlers read leftovers),
 * the response data are zeroed before each command, a byte past the
 * response comes from what follows the header, and work that takes time
 * (GPIO, the e-flash, the reset) is done from the main loop afterwards.
 */
#include "ec.h"

#define RES_SUCCESS		0
#define RES_INVALID_COMMAND	1
#define RES_INVALID_PARAM	3
#define RES_INVALID_VERSION	6
#define RES_INVALID_CHECKSUM	7

/* the vendor's layout: request header, response header, data length */
static __xdata uint8_t req_hdr[9];		/* DA, ver, csum, cmd, cmd, cver, rsv, len, len */
static __xdata uint8_t resp[10 + 64];		/* I2C header, then the data */
#define resp_data	(resp + 10)
static __xdata uint8_t params[64];
static __xdata uint8_t result;
static __xdata uint16_t data_len;
static __xdata uint8_t rx_i, tx_i;

/* state the commands read or set */
uint8_t host_events;
static __xdata uint8_t evt[5];			/* 0x55: the last byte is the event */
static __xdata uint8_t kb_backlight;
static __xdata uint8_t cmd31[5], cmd32[5];
static __xdata uint8_t als_3e21, reg_3e22;
static __xdata uint8_t pd_info[10];
static __xdata uint8_t pd_state[2];
static __xdata uint8_t gpio_wr_num, gpio_wr_val, gpio_rd_num, gpio_val;
static __xdata uint8_t autoflag_new;
static __xdata uint8_t target_rpm[4];
static __xdata uint8_t cmd3e0f[2];
static volatile __xdata uint8_t pending;
static __xdata uint16_t pending_at;
static __xdata uint8_t autoflag_last = 0xFF;	/* the vendor compares with what it last wrote */
#define P_RPM		0x02
#define P_GPIO_WR	0x04
#define P_GPIO_RD	0x08
#define P_AUTOFLAG	0x10
#define P_RESET		0x80

static __code const uint8_t proto_info[12] = {
	0x00, 0x00, 0x00, 0x08, 0x00, 0x49, 0x00, 0x4A, 0x00, 0x00, 0x00, 0x00
};

void host_event(uint8_t e)
{
	evt[4] = e;
	host_events |= e;
	EC_ACPI_INT(1);
}

static void gpio_write(uint8_t num, uint8_t v)
{
	uint8_t port = num >> 3, bit = num & 7;

	if (num >= 0xC8 || port > PJ)	/* IO expander (none) or no such pin */
		return;
	if (v)
		GPDR(port) |= 1 << bit;
	else
		GPDR(port) &= ~(1 << bit);
	gpio_val = v ? 1 : 0;
}

static void dispatch(uint16_t cmd)
{
	uint8_t i;

	switch (cmd) {
	case 0x000B:				/* GET_PROTOCOL_INFO */
		for (i = 0; i < 12; i++)
			resp_data[i] = proto_info[i];
		data_len = 12;
		break;
	case 0x0020:				/* PWM_GET_FAN_TARGET_RPM: the measured speed */
		{
			uint16_t r = fan_rpm();
			resp_data[0] = 0;
			resp_data[1] = 0;
			resp_data[2] = r >> 8;
			resp_data[3] = r;
		}
		data_len = 4;
		break;
	case 0x0021:				/* PWM_SET_FAN_TARGET_RPM */
		for (i = 0; i < 4; i++)
			target_rpm[i] = params[i];
		data_len = 0;
		pending |= P_RPM;
		break;
	case 0x0022:				/* keyboard backlight (none on the O6) */
		resp_data[0] = kb_backlight;
		resp_data[1] = 0;
		data_len = 2;
		break;
	case 0x0023:
		kb_backlight = params[0];
		DCR0 = (uint8_t)(((uint16_t)params[0] * 255) / 100);
		data_len = 0;
		break;
	case 0x0025:				/* PWM_SET_DUTY: percent in byte 1 */
		data_len = 0;
		rpm_mode = 0;
		fan_set_duty(((uint16_t)params[0] << 8) | params[1]);
		break;
	case 0x0026:				/* PWM_GET_DUTY */
		resp_data[0] = 0;
		resp_data[1] = (uint8_t)(((uint16_t)DCR2 * 100) / 255);
		data_len = 2;
		if (params[1])			/* the vendor checks byte 1 of the buffer */
			result = RES_INVALID_PARAM;
		break;
	case 0x002C:
		data_len = 1;			/* the vendor copies the cleared data byte */
		break;
	case 0x0031:
		for (i = 0; i < 5; i++)
			cmd31[i] = params[i];
		data_len = 0;
		break;
	case 0x0032:
		for (i = 0; i < 5; i++)
			resp_data[i] = cmd32[i];
		data_len = 5;
		break;
	case 0x0052:				/* THERMAL_AUTO_FAN_CTRL: the fan mode */
		fan_mode = params[0];
		data_len = 0;
		break;
	case 0x0055:				/* INT_GET_INFO: the pending event */
		for (i = 0; i < 5; i++)
			resp_data[i] = evt[i];
		data_len = 5;
		EC_ACPI_INT(0);
		for (i = 0; i < 5; i++)
			evt[i] = 0;
		host_events = 0;
		break;
	case 0x0092:				/* INT_WRITE_GPIO */
		/* a write still pending is done now, so back-to-back writes
		 * (UEFI sets several in a row) are not lost */
		if (pending & P_GPIO_WR)
			gpio_write(gpio_wr_num, gpio_wr_val);
		gpio_wr_num = params[0];
		gpio_wr_val = params[1];
		data_len = 0;
		pending |= P_GPIO_WR;
		break;
	case 0x0093:				/* INT_READ_GPIO: the last value read */
		resp_data[0] = gpio_val;
		data_len = 1;
		gpio_rd_num = params[0];
		pending |= P_GPIO_RD;
		break;
	case 0x3E01:				/* board ID */
	case 0x3E03:				/* PMIC versions */
	case 0x3E07:				/* PD versions */
		data_len = cmd == 0x3E01 ? 2 : cmd == 0x3E03 ? 3 : 4;
		if (cmd == 0x3E07) {
			resp_data[0] = pd_ver[0];
			resp_data[1] = pd_ver[1];
		}
		break;
	case 0x3E06:				/* GreenPAK version */
	case 0x3E0A:				/* FRAM ID: GPM2 (RSV2) */
		data_len = 1;
		if (cmd == 0x3E0A)
			resp_data[0] = (GPDRM >> 2) & 1;
		break;
	case 0x3E09:				/* power-off reason */
		resp_data[0] = poweroff_reason;
		data_len = 1;
		break;
	case 0x3E0B:				/* EC_AUTO_POWER_ON: the AC-loss policy */
		autoflag_new = params[0];
		data_len = 0;
		pending |= P_AUTOFLAG;
		break;
	case 0x3E0C:				/* the SoC temperature: degrees, hundredths */
		resp_data[0] = soc_temp;
		resp_data[1] = 0;
		data_len = 2;
		break;
	case 0x3E0D:				/* PD firmware update information */
		for (i = 0; i < 10; i++)
			pd_info[i] = params[i];
		data_len = 0;
		EC_ACPI_INT(0);
		break;
	case 0x3E0E:				/* PD firmware update state */
		data_len = 1;
		if (params[0] < 2)
			resp_data[0] = pd_state[params[0]];
		break;
	case 0x3E0F:
		cmd3e0f[0] = params[0];
		cmd3e0f[1] = params[1];
		if (params[0] == 0x2A) {	/* the vendor's RSM_RST control */
			if (params[1] == 1)
				RSM_RST(1);
			else
				RSM_RST(0);
		}
		data_len = 0;
		break;
	case 0x3E10:				/* PD firmware data: not supported */
		data_len = 0;
		EC_ACPI_INT(0);
		break;
	case 0x3E13:
		resp_data[0] = 0;
		data_len = 1;
		break;
	case 0x3E19:				/* FORCE_EC_RESET */
		data_len = 0;
		pending |= P_RESET;
		break;
	case 0x3E20:				/* the lid switch, inverted */
		resp_data[0] = LID_SW_L() ? 0 : 1;
		data_len = 1;
		break;
	case 0x3E21:
		als_3e21 = params[0];
		data_len = 0;
		break;
	case 0x3E22:
		reg_3e22 = params[0];
		data_len = 0;
		break;
	case 0x0600:				/* battery: none */
	case 0x0601:
	case 0x3E02:				/* charger: none */
	case 0x3E14:
		data_len = 0;
		break;
	case 0x3FFF: {				/* GET_EC_VERSION */
		static __code const char v[19] = VERSION_STRING;
		for (i = 0; i < 19; i++)
			resp_data[i] = v[i];
		data_len = 19;
		break;
	}
	default:
		result = RES_INVALID_COMMAND;
		break;
	}
}

static void run_command(void)
{
	uint8_t i, sum;
	uint16_t len = ((uint16_t)req_hdr[7] << 8) | req_hdr[8];

	sum = 0;
	for (i = 1; i < 9; i++)
		sum += req_hdr[i];
	for (i = 0; i < len && i < 64; i++)
		sum += params[i];
	if (sum)
		result = RES_INVALID_CHECKSUM;
	for (i = 0; i < 64; i++)
		resp_data[i] = 0;
	for (i = 0; i < 10; i++)
		resp[i] = 0;
	resp[2] = 3;
	data_len = 0;
	if (result == RES_SUCCESS)
		dispatch(((uint16_t)req_hdr[3] << 8) | req_hdr[4]);
	resp[0] = result;
	resp[4] = 0;
	resp[5] = result;
	resp[6] = data_len >> 8;
	resp[7] = data_len;
	resp[1] = (uint8_t)data_len + 10;
	sum = 0;
	for (i = 2; i < 10; i++)
		sum += resp[i];
	for (i = 0; i < data_len && i < 64; i++)
		sum += resp_data[i];
	resp[3] = (uint8_t)-sum;
	pending_at = ms_now + 15;
}

static void rx_byte(uint8_t i, uint8_t b)
{
	if (i && req_hdr[0] != 0xDA)
		return;
	if (i == 0) {
		result = b == 0xDA ? RES_SUCCESS : RES_INVALID_COMMAND;
	} else if (i == 1 && b != 3) {
		result = RES_INVALID_VERSION;
	}
	if (i < 9)
		req_hdr[i] = b;
	else if (i - 9 < 64)
		params[i - 9] = b;
	if (i >= 8 && i == 8 + (((uint16_t)req_hdr[7] << 8) | req_hdr[8]))
		run_command();
}

static uint8_t tx_byte(uint8_t i)
{
	if (i < 10)
		return resp[i];
	if (!data_len)			/* the vendor's next variable: the target RPM */
		return i < 14 ? target_rpm[i - 10] : 0;
	if (i - 10 < 64)
		return resp_data[i - 10];
	return 0;
}

/* SMBus B slave interrupt (the vendor's order: acknowledge, then the data) */
void host_isr(void)
{
	uint8_t st = SLSTAB, b;

	if (st & 0x02) {
		SLSTAB = 0x02;
		if (SLSTAB & 0x10) {
			if (SLSTAB & 0x08) {
				b = tx_byte(tx_i);
				SLDAB = b;
				SLDAB = b;
				tx_i++;
			} else {
				b = SLDAB;
				rx_byte(rx_i, b);
				(void)SLDAB;
				rx_i++;
			}
		}
	}
	if (SLSTAB & 0x20) {
		SLSTAB = 0x20;
		rx_i = 0;
		tx_i = 0;
	}
	if (SLSTAB & 0x04)
		SLSTAB = 0x04;
}

void host_init(void)
{
	/* channels A and B as the vendor sets them: slave addresses 0x1D and
	 * 0x76 (RESLADR / RESLADR2), slave interrupts on; only B is served */
	XREG(0x1C10) = 0x21;
	IER(1) = 0x0A;
	XREG(0x1C21) = 0x21;
	IER(1) = 0x0E;
	XREG(0x1C10) = 0x20;
	SICRA = 0x0F;
	RESLADRA = 0x3A;
	RESLADR2A = 0xF6;
	XREG(0x1C01) = 0x02;
	XREG(0x1C01) = 0x00;
	XREG(0x1C00) = 0xFF;
	SLSTAA = 0xFF;
	XREG(0x1C21) = 0x20;
	SICRB = 0x0F;
	RESLADRB = 0x3A;
	RESLADR2B = 0xF6;
	XREG(0x1C12) = 0x02;
	XREG(0x1C12) = 0x00;
	XREG(0x1C11) = 0xFF;
	SLSTAB = 0xFF;
	IER(1) = 0x04;			/* only B */
}


static void gpio_read(uint8_t num)
{
	uint8_t port = num >> 3, bit = num & 7;

	if (num >= 0xC8 || port > PJ) {
		gpio_val = 0;
		return;
	}
	gpio_val = (GPDR(port) >> bit) & 1;
}

/*
 * Work a command leaves for later, at the vendor's latencies: the
 * target speed, GPIO reads, the AC-loss flag and the reset ~15 ms after
 * the command; GPIO writes in the 100 ms hook (so a read right after a
 * write still sees the old level, as with the vendor).
 */

void host_poll(void)
{
	uint8_t p;

	trip_flag = 0;
	if (!(pending & ~P_GPIO_WR) || !after(pending_at))
		return;
	EA = 0;
	p = pending & ~P_GPIO_WR;
	pending &= P_GPIO_WR;
	EA = 1;
	if (p & P_RPM) {
		/* the vendor's first step towards a target speed: duty =
		 * max(target, 2500) / 21, at most 255; then fan.c corrects it
		 * from the measured speed */
		uint16_t t = ((uint16_t)target_rpm[2] << 8) | target_rpm[3];

		if (target_rpm[0] || target_rpm[1])
			t = 0xFFFF;
		if (t < 2500)
			t = 2500;
		t /= 21;
		DCR2 = t > 255 ? 255 : (uint8_t)t;
		rpm_target = ((uint16_t)target_rpm[2] << 8) | target_rpm[3];
		rpm_mode = 1;
	}
	if (p & P_GPIO_RD)
		gpio_read(gpio_rd_num);
	if (p & P_AUTOFLAG) {
		auto_power_on = autoflag_new;
		if (autoflag_new != autoflag_last) {
			autoflag_last = autoflag_new;
			eflash_write_byte(0x1F8FF, autoflag_new);
		}
	}
	if (p & P_RESET)
		power_force_reset();
}

void host_poll_100ms(void)
{
	if (!(pending & P_GPIO_WR))
		return;
	EA = 0;
	pending &= ~P_GPIO_WR;
	EA = 1;
	gpio_write(gpio_wr_num, gpio_wr_val);
}
