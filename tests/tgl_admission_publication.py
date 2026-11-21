from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'WhateverXe/kern_host.cpp').read_text()
p=s[s.index('void preparePageFix('):s.index('void XeHost::init()')]
assert 'publishPageAdmission(gpu,admission)' in p
assert p.index('publishPageAdmission(gpu,admission)') < p.index('if(!probe && !xfer')
for status in ['SymbolMissing','FingerprintRejected','PolicyRejected','RouteFailed','Routed']:
 assert 'AdmissionStatus::'+status in p
assert 'if(!gpu || !gpu->setProperty("TGLPageFixAdmission"' in s
assert 'admission publication failed' in s
assert 'if(!gpu->setProperty("TGLPageFixLive",live))' in p
assert 'validatorFingerprint(reinterpret_cast<const void *>(code),308,admission.computed)' in p
print('admission publication exits and fallback ok')
