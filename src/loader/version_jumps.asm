; SPDX-License-Identifier: GPL-2.0-or-later
; version.dll proxy exports: each one jumps to the System32 function (g_real filled in DllMain).
EXTERN g_real:QWORD

PROXY MACRO name, idx
PUBLIC px_&name
px_&name PROC
    jmp qword ptr [g_real + idx*8]
px_&name ENDP
ENDM

.code
PROXY GetFileVersionInfoA, 0
PROXY GetFileVersionInfoByHandle, 1
PROXY GetFileVersionInfoExA, 2
PROXY GetFileVersionInfoExW, 3
PROXY GetFileVersionInfoSizeA, 4
PROXY GetFileVersionInfoSizeExA, 5
PROXY GetFileVersionInfoSizeExW, 6
PROXY GetFileVersionInfoSizeW, 7
PROXY GetFileVersionInfoW, 8
PROXY VerFindFileA, 9
PROXY VerFindFileW, 10
PROXY VerInstallFileA, 11
PROXY VerInstallFileW, 12
PROXY VerLanguageNameA, 13
PROXY VerLanguageNameW, 14
PROXY VerQueryValueA, 15
PROXY VerQueryValueW, 16
END
