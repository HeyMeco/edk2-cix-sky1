/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * CIX's PID controller (Source/common/utilities/u_pid.c).  The input is
 * filtered (first order, in_alpha); the error to the set point (plus an
 * amendment) drives a PID whose output, scaled, is the feedback.  pid_init
 * and pid_reset are not in CIX's image (unused); the controllers are
 * initialised data (src/dpm/dpm_tables.c).
 */
#include "dpm_rt.h"

/* CIX's vAssertCalledWithTRC with its trace codes */
#define PID_ASSERT(c)		do { if (!(c)) __builtin_trap(); } while (0)

/* The input filter for a time constant in seconds (1 ms iterations) */
void pid_updateTimeConst(T_PID_DATA *pid, float TimeConst)
{
	float q = 0.001f / TimeConst;

	pid->in_alpha = !(0.001f >= TimeConst) ? q : 1.0f;
}

void pid_setParams(T_PID_DATA *pid, float Kp, float Ki, float Kd)
{
	PID_ASSERT(pid);		/* CIX: 0xf1850064 */
	pid->Kp = Kp;
	pid->Ki = Ki;
	pid->Kd = Kd;
}

void pid_setSetPoint(T_PID_DATA *pid, float SetPoint)
{
	PID_ASSERT(pid);
	pid->SetPoint = SetPoint;
}

void pid_setAmend(T_PID_DATA *pid, float SetAmend)
{
	PID_ASSERT(pid);
	pid->SetAmend = SetAmend;
}

void pid_setFbScale(T_PID_DATA *pid, float FbScale)
{
	PID_ASSERT(pid);
	pid->fb_scale = FbScale;
}

typedef union {
	float f;
	uint32_t u;
} fbits;

/*
 * One step.  CIX's oddity kept: when the filtered input rounds back to the
 * last one while the input differs, the filter would stick; it is moved one
 * unit in the last place towards the input (the mantissa adjusted as an
 * integer).
 */
float pid_iteration(T_PID_DATA *pid)
{
	float cur, err;
	fbits in;

	PID_ASSERT(pid);		/* CIX: 0xf1850090 */
	cur = *pid->pInput;
	pid->CurInput = cur;
	in.f = cur * pid->in_alpha + (1.0f - pid->in_alpha) * pid->input_last;
	if (pid->input_last == in.f && cur != in.f) {
		if (!(cur > in.f))		/* CIX: ULE.S */
			in.u -= (in.u & 0x7fffffffu) != 0;
		else
			in.u++;
	}
	err = pid->SetPoint + pid->SetAmend - in.f;
	pid->input_last = in.f;
	pid->integral += err;
	pid->u_sum = err * pid->Kp + pid->integral * pid->Ki + pid->Kd * (err - pid->err_last);
	pid->err = err;
	pid->err_last = err;
	pid->FeedBack = pid->u_sum * pid->fb_scale;
	return pid->FeedBack;
}
