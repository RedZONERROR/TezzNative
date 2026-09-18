const statusEl = document.getElementById('status');
const promptEl = document.getElementById('prompt');
const tplEl = document.getElementById('template');
const metaEl = document.getElementById('meta');
const codeEl = document.getElementById('code');
const genBtn = document.getElementById('generate');
const copyBtn = document.getElementById('copy');

async function checkHealth() {
  try {
    const res = await fetch('/health', { cache: 'no-store' });
    const data = await res.json();
    if (data && data.ok) {
      statusEl.textContent = `server online (${data.status || 'ok'})`;
      statusEl.classList.add('ok');
      return;
    }
  } catch (_) {}
  statusEl.textContent = 'server offline';
  statusEl.classList.remove('ok');
}

async function generate() {
  const prompt = (promptEl.value || '').trim();
  if (!prompt) {
    codeEl.textContent = 'Write a prompt first.';
    return;
  }
  codeEl.textContent = 'Generating...';
  metaEl.textContent = '';
  const payload = {
    prompt,
    template: tplEl.value || 'auto'
  };
  try {
    const res = await fetch('/ai/code', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    const data = await res.json();
    if (!data || !data.ok) {
      codeEl.textContent = 'Generation failed.';
      return;
    }
    metaEl.textContent = `template: ${data.template || 'auto'}\nsource: ${data.source || '(model)'}`;
    codeEl.textContent = data.code || '';
  } catch (e) {
    codeEl.textContent = `Request failed: ${e.message || e}`;
  }
}

async function copyCode() {
  const text = codeEl.textContent || '';
  if (!text) return;
  try {
    await navigator.clipboard.writeText(text);
    copyBtn.textContent = 'Copied';
    setTimeout(() => (copyBtn.textContent = 'Copy'), 900);
  } catch (_) {
    copyBtn.textContent = 'Copy failed';
    setTimeout(() => (copyBtn.textContent = 'Copy'), 900);
  }
}

genBtn.addEventListener('click', generate);
copyBtn.addEventListener('click', copyCode);
checkHealth();
