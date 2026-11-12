#!/usr/bin/env python3
"""Run source/mock regressions without GPU access, private firmware or boot config."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
for name in ('run_selector_guard_test.py','run_reset_containment_test.py'):
    subprocess.run([sys.executable,str(root/'tools'/name)],cwd=root,check=True)
(root/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fault-test-',dir=root/'build') as d:
    exe=Path(d)/'test-selector-fault.exe'
    flags=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-DGU_UEFI_PORT','-DGU_AD104_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL','-Icompat','-Ivendor/edk2/Include','-Ivendor/edk2/Include/X64','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','tests/test_selector_fault.c','-o',str(exe)]
    subprocess.run(flags,cwd=root,check=True)
    subprocess.run([str(exe)],cwd=root,check=True)
print('All public host regressions passed; no physical GPU access')
