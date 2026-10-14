import copy
import http.client
import json
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch
from macro_input import validate_program, execute, NativeInput
from keypad_companion import MacroRunner, make_handler
from keypad_usb import DeviceError

class Backend:
    def __init__(self):self.events=[]
    def prepare(self,p):self.events.append(('prepare',len(p['steps'])))
    def chord(self,keys):self.events.append(('keys',keys))
    def type_text(self,text):self.events.append(('text',text))
class Device:
    def __init__(self):self.calls=[];self.fail_ack=False
    def json(self,path,body=None):
        self.calls.append((path,body))
        if path.endswith('/ack') and self.fail_ack:raise DeviceError(503,'uncertain ack')
        return {'device':'PCB2 Keypad','config':{},'recording':{}}
    def request(self,path,body=None,headers=None):self.calls.append((path,body));return b'{"ok":true}',{'Content-Type':'application/json'}
PROGRAM={'name':'中文回覆','steps':[{'type':'keys','keys':['CTRL','A','B','C','D','E','F','G']},{'type':'wait','ms':200},{'type':'text','text':'您好😀\n收到。'}]}
class Tests(unittest.TestCase):
    def test_unicode_and_many_keys(self):
        backend=Backend();wait=[];execute(PROGRAM,backend,threading.Event(),lambda seconds:wait.append(seconds))
        self.assertEqual(backend.events[-1],('text',PROGRAM['steps'][-1]['text']));self.assertEqual(wait,[.2]);self.assertEqual(len(backend.events[1][1]),8)
    def test_reject_all_before_input(self):
        for change in [dict(type='exec',command='anything'),dict(type='text',text='\0'),dict(type='wait',ms=True),dict(type='keys',keys=['A','A'])]:
            p=copy.deepcopy(PROGRAM);p['steps'].append(change);backend=Backend()
            with self.assertRaises(ValueError):execute(p,backend,threading.Event())
            self.assertEqual(backend.events,[])
    def test_ack_before_input_and_no_input_on_uncertainty(self):
        device=Device();backend=Backend();runner=MacroRunner(device,backend)
        runner.handle({'id':1,'key':0,'program':PROGRAM})
        self.assertEqual([c[0] for c in device.calls],['/api/macro/host','/api/macro/ack','/api/macro/host']);self.assertTrue(backend.events)
        self.assertEqual(runner.diagnostics(),{'received':1,'completed':1,'failed':0,'last_key':1,'result':'completed'})
        device.fail_ack=True;backend.events.clear()
        with self.assertRaises(DeviceError):runner.handle({'id':2,'key':0,'program':PROGRAM})
        self.assertEqual(backend.events,[]);self.assertFalse(runner.busy.is_set())
        self.assertEqual(runner.diagnostics()['failed'],1)
        self.assertEqual(runner.diagnostics()['completed'],1)
        self.assertNotIn('program',runner.diagnostics())
    def test_reconnection_clears_usb_error_but_keeps_input_error(self):
        device=Device();runner=MacroRunner(device,Backend())
        with patch.object(device,'json',side_effect=DeviceError(503,'USB occupied')):
            runner.refresh_host()
        self.assertFalse(runner.ready.is_set());self.assertEqual(runner.error,'USB occupied')
        runner.refresh_host()
        self.assertTrue(runner.ready.is_set());self.assertEqual(runner.error,'')
        runner.input_error='Input denied'
        runner.refresh_host()
        self.assertEqual(runner.error,'Input denied')
    def test_cancel_during_wait(self):
        closing=threading.Event();backend=Backend()
        def stop(_):closing.set();return True
        execute(PROGRAM,backend,closing,stop)
        self.assertFalse(any(e[0]=='text' for e in backend.events))
    def test_release_all_after_partial_failure(self):
        class Controller:
            def __init__(self):self.events=[]
            def press(self,key):
                self.events.append(('press',key))
                if key=='b':raise RuntimeError('injection failed')
            def release(self,key):self.events.append(('release',key))
        backend=NativeInput.__new__(NativeInput);backend.wayland=False;backend.controller=Controller()
        with self.assertRaises(RuntimeError):backend.chord(['A','B'])
        self.assertEqual(backend.controller.events[-1],('release','a'))
    def test_wayland_text_stdin(self):
        backend=NativeInput.__new__(NativeInput);backend.wayland=True
        with patch('macro_input.subprocess.run') as run:
            backend.type_text('-中文😀');args,kwargs=run.call_args
            self.assertEqual(args[0],['wtype','-d','5','-']);self.assertEqual(kwargs['input'],'-中文😀'.encode());self.assertNotIn('shell',kwargs)
    def test_local_server_rejects_remote_origin_and_rebinding(self):
        device=Device();assets=Path(__file__).resolve().parents[1]/'dist-web/pcb2'
        server=ThreadingHTTPServer(('127.0.0.1',0),make_handler(device,None,assets,0));port=server.server_port
        server.RequestHandlerClass=make_handler(device,None,assets,port);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            def request(method,path,headers=None,body=None):
                connection=http.client.HTTPConnection('127.0.0.1',port,timeout=2);connection.request(method,path,body,headers or {});response=connection.getresponse();status=response.status;data=response.read();connection.close();return status,data
            self.assertEqual(request('GET','/pcb2/')[0],200)
            self.assertEqual(request('GET','/api/state',{'Host':'evil.example'})[0],403)
            self.assertEqual(request('POST','/api/config',{'Origin':'https://evil.example','X-PCB2-Control':'1','Content-Type':'application/json'},'{}')[0],403)
            self.assertEqual(request('POST','/api/config',{},'{}')[0],403);self.assertEqual(device.calls,[])
            self.assertEqual(request('GET','/pcb2/../../README.md')[0],404)
            self.assertEqual(request('GET','/api/macro/event')[0],404)
            self.assertEqual(request('POST','/api/macro',{'X-PCB2-Control':'1','Content-Type':'application/json'},'{"key":0}')[0],200)
        finally:server.shutdown();server.server_close();thread.join()
if __name__=='__main__':unittest.main()
