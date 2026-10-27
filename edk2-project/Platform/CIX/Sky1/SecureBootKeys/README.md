# Secure Boot default keys

`Microsoft/` holds Microsoft's public UEFI Secure Boot certificates, copied
unchanged from [microsoft/secureboot_objects](https://github.com/microsoft/secureboot_objects)
at commit `811a9086c86f7c2c26f520414c31d2bd8494188c` (`PreSignedObjects/`).
They are only certificates (no private keys). A board uses them as its
default PK/KEK/db by pointing the `*_DEFAULT_FILE*` defines at them (see
`MGP1WSB/MGP1WSB.dsc`). The firmware does not enroll them by itself: in setup,
Device Manager → Secure Boot Configuration → Reset Secure Boot Keys.

| Variable | File | Certificate | SHA-256 |
| --- | --- | --- | --- |
| PK  | `WindowsOEMDevicesPK.der` | Windows OEM Devices PK | `2f569e8edaf9657dc4951c29598725255c7f821472db71374211fe44d082546f` |
| KEK | `MicCorKEKCA2011_2011-06-24.der` | Microsoft Corporation KEK CA 2011 | `a1117f516a32cefcba3f2d1ace10a87972fd6bbe8fe0d0b996e09e65d802a503` |
| KEK | `MicCorKEK2kCA2023.der` | Microsoft Corporation KEK 2K CA 2023 | `3cd3f0309edae228767a976dd40d9f4affc4fbd5218f2e8cc3c9dd97e8ac6f9d` |
| db  | `MicWinProPCA2011_2011-10-19.der` | Microsoft Windows Production PCA 2011 | `e8e95f0733a55e8bad7be0a1413ee23c51fcea64b3c8fa6a786935fddcc71961` |
| db  | `WindowsUEFICA2023.der` | Windows UEFI CA 2023 | `076f1fea90ac29155ebf77c17682f75f1fdd1be196da302dc8461e350a9ae330` |
| db  | `MicCorUEFCA2011_2011-06-27.der` | Microsoft Corporation UEFI CA 2011 | `48e99b991f57fc52f76149599bff0a58c47154229b9f8d603ac40d3500248507` |
| db  | `MicUEFICA2023.der` | Microsoft UEFI CA 2023 | `f6124e34125bee3fe6d79a574eaa7b91c0e7bd9d929c1a321178efd611dad901` |
| db  | `MicOptionROMUEFICA2023.der` | Microsoft Option ROM UEFI CA 2023 | `e5be3e64c6e66a281457ecdece0d6d0787577aad2a3a0144262c10c14ba8d8f1` |

The PK is the one Microsoft publishes for OEM devices that leave PK
management to Microsoft; with it, only Microsoft-signed KEK updates are
accepted. To use your own keys, clear the keys in Secure Boot Configuration
(setup mode) and enroll yours instead.
