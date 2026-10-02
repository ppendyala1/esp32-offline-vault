#!/usr/bin/env python3
"""Computer-only encrypted vault demo. Uses local system time; does not emulate the RTC or firmware UI."""
import base64
import binascii
import getpass
import hashlib
import hmac
import json
from pathlib import Path
import secrets
import struct
import time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
FILE=Path(__file__).resolve().parents[1]/'build'/'desktop-demo.bin'
AAD=b'offline-vault-desktop-demo-v1'

def prompt_secret(s):return getpass.getpass(s)

def save(accounts,password):
    FILE.parent.mkdir(exist_ok=True)
    salt=secrets.token_bytes(16);nonce=secrets.token_bytes(12)
    key=hashlib.pbkdf2_hmac('sha256',password.encode(),salt,100000,32)
    blob=salt+nonce+AESGCM(key).encrypt(nonce,json.dumps(accounts).encode(),AAD)
    tmp=FILE.with_suffix('.tmp');tmp.write_bytes(blob);tmp.replace(FILE)

def code(secret,now):
    text=''.join(secret.upper().split());key=base64.b32decode(text+'='*((-len(text))%8))
    digest=hmac.new(key,struct.pack('>Q',int(now)//30),hashlib.sha1).digest();off=digest[-1]&15
    return f'{(struct.unpack(">I",digest[off:off+4])[0]&0x7fffffff)%1000000:06d}'

def main():
    print('Computer-only demo. Start with dummy accounts. Firmware uses a different flash format.')
    password=prompt_secret('Master password: ')
    if FILE.exists():
        blob=FILE.read_bytes();key=hashlib.pbkdf2_hmac('sha256',password.encode(),blob[:16],100000,32)
        try:accounts=json.loads(AESGCM(key).decrypt(blob[16:28],blob[28:],AAD))
        except Exception:print('Wrong password or damaged file.');return
    else:
        if len(password)<12:print('Use at least 12 characters.');return
        if password!=prompt_secret('Confirm: '):print('Passwords do not match.');return
        accounts=[];save(accounts,password)
    last=time.monotonic()
    while True:
        cmd=input('list / add / code / delete / lock > ').strip().lower()
        if time.monotonic()-last>=60:print('Session expired. Restart to unlock.');return
        last=time.monotonic()
        if cmd=='lock':return
        if cmd=='list':
            for i,a in enumerate(accounts,1):print(i,a['name'])
        elif cmd=='add':
            a={'name':input('Name: '),'username':input('Username: '),'password':prompt_secret('Password: '),
               'secret':prompt_secret('TOTP Base32 secret (optional): ')}
            if a['secret']:
                try:code(a['secret'],time.time())
                except (ValueError,binascii.Error):print('Invalid Base32.');continue
            accounts.append(a);save(accounts,password);print('Encrypted and saved.')
        elif cmd in ('code','delete'):
            try:i=int(input('Account number: '))-1;assert 0<=i<len(accounts)
            except (ValueError,AssertionError):print('Invalid account.');continue
            if cmd=='code':
                if not accounts[i]['secret']:print('No TOTP secret.');continue
                now=int(time.time());print(code(accounts[i]['secret'],now),'valid for',30-now%30,'seconds')
            elif input('Type DELETE: ')=='DELETE':accounts.pop(i);save(accounts,password)

if __name__=='__main__':
    try:main()
    except (KeyboardInterrupt,EOFError):print('\nLocked.')
