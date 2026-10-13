import importlib.util
import json
from pathlib import Path
from unittest.mock import patch

root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('keypad_usb',root/'tools/keypad_usb.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class Fake(module.UsbDevice):
    def __init__(self,results):
        super().__init__();self.results=iter(results);self.calls=[]
    def rpc(self,path,body=None):
        self.calls.append((path,body));item=next(self.results)
        if isinstance(item,Exception):raise item
        return item
with patch.object(module.time,'sleep'):
    for failure in (OSError('link interrupted'),module.DeviceError(503,'USB 未回應；請確認 PCB'),module.DeviceError(503,'ESP_ERR_INVALID_CRC')):
        link=Fake([failure,{'ok':True}])
        result,_=link.request('/api/sound/chunk',b'{"owner":"1234567890123456","offset":0,"pcm":"AAAAAA==","crc32":558161692}')
        assert json.loads(result)['ok'] and len(link.calls)==2 and link.calls[0]==link.calls[1]
    for path,failure in (('/api/sound/chunk',module.DeviceError(503,'ESP_ERR_NO_MEM')),
                         ('/api/trigger',OSError('link interrupted'))):
        link=Fake([failure,{'ok':True}])
        try:link.request(path,b'{"pcm":"AAAAAA==","crc32":558161692}')
        except Exception as error:assert error is failure
        else:raise AssertionError('Unsafe retry performed')
        assert len(link.calls)==1
    link=Fake([module.DeviceError(503,'USB 未回應') for _ in range(5)])
    try:link.request('/api/sound/commit',b'{}')
    except module.DeviceError:pass
    else:raise AssertionError('Retries not bounded')
    assert len(link.calls)==5
    link=Fake([{'ok':True}])
    try:link.request('/api/sound/chunk',b'{"pcm":"AAAAAA==","crc32":1}')
    except module.DeviceError as error:assert error.status==400
    else:raise AssertionError('Invalid outgoing CRC accepted')
    assert not link.calls
print('PASS: sound transient retries preserve arguments; memory/trigger failures not retried; retries bounded')
