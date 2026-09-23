from pathlib import Path
p = Path('tests/probe/probe_hooks.cpp')
s = p.read_text(encoding='utf-8')
repls = {
'    "<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\\r\\n"\n': '    "<?xml version=\\"1.0\\" encoding=\\"UTF-8\\" standalone=\\"yes\\"?>\\r\\n"\n',
'    "<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">\\r\\n"\n': '    "<assembly xmlns=\\"urn:schemas-microsoft-com:asm.v1\\" manifestVersion=\\"1.0\\">\\r\\n"\n',
'    "<dependency><dependentAssembly><assemblyIdentity type="win32" "\n': '    "<dependency><dependentAssembly><assemblyIdentity type=\\"win32\\" "\n',
'    "name="Microsoft.Windows.Common-Controls" version="6.0.0.0" "\n': '    "name=\\"Microsoft.Windows.Common-Controls\\" version=\\"6.0.0.0\\" "\n',
'    "processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/>"\n': '    "processorArchitecture=\\"*\\" publicKeyToken=\\"6595b64144ccf1df\\" language=\\"*\\"/>"\n',
}
for a,b in repls.items():
    if s.count(a) != 1:
        raise SystemExit(f'expected one match for {a!r}, got {s.count(a)}')
    s = s.replace(a,b,1)
p.write_text(s, encoding='utf-8', newline='')
print('manifest quoting fixed')
