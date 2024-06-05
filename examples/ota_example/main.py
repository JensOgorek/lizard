#!/usr/bin/env python3
import os
from glob import glob

from nicegui import app, ui
from starlette.requests import Request
from starlette.responses import FileResponse, Response

ui.markdown('# Lizard OTA Demo')

def get_binary():
    bins = glob('../../build/*.bin')
    log.push(f'found {len(bins)} binaries: {bins}')
    bin = next((b for b in bins if 'lizard' in b), None)    
    log.push(f'offering binary: {bin}')
    return bin

@app.get('/ota/binary')
def ota_binary(request: Request) -> Response:
    log.push('request received from ' + request.client.host)
    try:
        bin = get_binary()
        if bin is None:
            log.push('binary not found')
            return Response(status_code=503, content='Lizard binary not found')
        log.push(f'send binary to {request.client.host}')
        return FileResponse(os.path.abspath(bin))
    except Exception as e:
        log.push(f'Error: {str(e)}')
        return Response(status_code=503, content='Something went wrong')

log = ui.log().classes('w-full h-96')

log.push('ready to serve OTA updates')
log.push('--------------------------------')

ui.run(port=1111)