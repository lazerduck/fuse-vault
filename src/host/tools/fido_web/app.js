'use strict';
const $ = id => document.getElementById(id);
let active;
const decode = s => Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')), c => c.charCodeAt(0));
const encode = buffer => btoa(String.fromCharCode(...new Uint8Array(buffer))).replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,'');
async function api(path, data) {
  const r = await fetch(path, data === undefined ? {} : {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
  const value = await r.json(); if (!r.ok) throw new Error(value.error || 'Request failed'); return value;
}
async function refresh() {
  const {accounts} = await api('/api/accounts'); $('accounts').replaceChildren();
  for (const a of accounts) { const li=document.createElement('li'); li.textContent=`${a.name} · ${a.credentials} credential(s)`; $('accounts').append(li); }
  if (!accounts.length) {const li=document.createElement('li');li.textContent='No accounts registered yet.';$('accounts').append(li);}
}
async function run(kind) {
  active = new AbortController(); ['register','login','discover'].forEach(id=>$(id).disabled=true); $('cancel').disabled=false;
  $('details').textContent=''; $('status').textContent='Waiting for your browser and device. Verify localhost on the device screen.';
  try {
    const options=await api(`/api/${kind}/begin`,{name:$('name').value}); const p=options.publicKey;
    p.challenge=decode(p.challenge); if(p.user)p.user.id=decode(p.user.id);
    for(const list of [p.excludeCredentials,p.allowCredentials])for(const c of list||[])c.id=decode(c.id);
    p.timeout=120000;
    const credential=await navigator.credentials[kind==='register'?'create':'get']({publicKey:p,signal:active.signal});
    const r=credential.response;
    const response={clientDataJSON:encode(r.clientDataJSON)};
    if(kind==='register'){response.attestationObject=encode(r.attestationObject);response.transports=r.getTransports?r.getTransports():['usb'];}
    else {response.authenticatorData=encode(r.authenticatorData);response.signature=encode(r.signature);response.userHandle=r.userHandle?encode(r.userHandle):null;}
    const result=await api(`/api/${kind}/complete`,{id:credential.id,rawId:encode(credential.rawId),type:credential.type,response,clientExtensionResults:credential.getClientExtensionResults()});
    $('status').textContent=`${kind==='register'?'Registration':'Login'} verified for ${result.account}.`;
    $('details').textContent=JSON.stringify(result,null,2); await refresh();
  } catch(error){$('status').textContent=`${error.name}: ${error.message}`;}
  finally {active=null;['register','login','discover'].forEach(id=>$(id).disabled=false);$('cancel').disabled=true;}
}
for(const kind of ['register','login','discover'])$(kind).addEventListener('click',()=>run(kind));
$('cancel').addEventListener('click',()=>active?.abort());
if(!window.PublicKeyCredential){$('status').textContent='WebAuthn is unavailable. Open this page at http://localhost:8000 in a supported browser.';['register','login','discover'].forEach(id=>$(id).disabled=true);}
refresh().catch(e=>$('status').textContent=e.message);
