let backend = null;

try {
  new QWebChannel(qt.webChannelTransport, function(channel) {
    backend = channel.objects.backend;
  });
} catch(e) {
  console.warn('QWebChannel not available');
}

const canvas = document.getElementById("canvas-bg");
const ctx = canvas.getContext("2d");
let w, h, particles = [];

function resizeCanvas() {
  w = window.innerWidth; h = window.innerHeight;
  canvas.width = w; canvas.height = h;
  particles = [];
  const count = Math.floor(Math.min(w, h) / 22);
  for (let i = 0; i < count; i++) {
    const r = Math.random() * 90 + 30;
    particles.push({
      x: Math.random() * w, y: Math.random() * h,
      dx: (Math.random() - 0.5) * 0.12, dy: (Math.random() - 0.5) * 0.12,
      r, alpha: Math.random() * 0.1 + 0.03
    });
  }
}

function drawCanvas() {
  ctx.clearRect(0, 0, w, h);
  particles.forEach(p => {
    ctx.beginPath();
    ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
    ctx.fillStyle = `rgba(224, 170, 160, ${p.alpha})`;
    ctx.fill();
    p.x += p.dx; p.y += p.dy;
    if (p.x + p.r > w || p.x - p.r < 0) p.dx *= -1;
    if (p.y + p.r > h || p.y - p.r < 0) p.dy *= -1;
  });
  requestAnimationFrame(drawCanvas);
}

resizeCanvas();
window.addEventListener('resize', resizeCanvas);
drawCanvas();

const translations = {
  en: {
    title: "EMPI Agent",
    subtitle: "Adaptive learning for everyone",
    langLabel: "RU",
    downloadBtn: "Download",
    copyBtn: "Copy",
    welcomeMsg: "Upload a file, paste a URL, or type your needs. I'll adapt content for you.",
    chatPlaceholder: "Describe your learning needs...",
    urlPlaceholder: "https://example.com/article...",
    fetchBtn: "Fetch",
    presetAdhd: "ADHD-friendly",
    presetDyslexia: "Dyslexia-friendly",
    presetChild: "For children",
    presetBeginner: "Beginner",
    tabOriginal: "Original",
    tabAdapted: "Adapted",
    emptyOriginal: "Upload a file or paste a URL to see the original content here",
    emptyAdapted: "Describe your needs and generate to see adapted content here",
    toggleLabel: "Adaptation:",
    toggleOn: "On",
    toggleOff: "Off",
    reapplyBtn: "Re-adapt",
    footerLine1: "EMPI Agent | Trajectory of Growth | 2026",
    footerLine2: "Made by Missvector",
    msgFileAttached: "File attached",
    msgUrlFetched: "Content fetched from URL",
    msgAdapting: "Adapting content...",
    msgAdapted: "Content adapted. View in the Adapted tab.",
    msgNeedSource: "Please upload a file or paste a URL first.",
    msgNeedPrompt: "Please describe your needs in the input field.",
    msgBackendNotReady: "Backend not ready, please wait...",
    msgErrorFetch: "Failed to fetch URL content.",
    msgCopied: "Copied.",
    msgDownloaded: "Downloaded.",
    msgReadapting: "Re-adapting...",
    alertInDevelopment: "This feature (URL fetch) is under development. Please upload a file instead."
  },
  ru: {
    title: "EMPI Agent",
    subtitle: "Адаптивное обучение для каждого",
    langLabel: "EN",
    downloadBtn: "Скачать",
    copyBtn: "Копировать",
    welcomeMsg: "Загрузите файл, вставьте URL или опишите ваши потребности. Я адаптирую материал.",
    chatPlaceholder: "Опишите ваши учебные потребности...",
    urlPlaceholder: "https://example.com/статья...",
    fetchBtn: "Загрузить",
    presetAdhd: "Для СДВГ",
    presetDyslexia: "Для дислексии",
    presetChild: "Для детей",
    presetBeginner: "Начинающим",
    tabOriginal: "Оригинал",
    tabAdapted: "Адаптация",
    emptyOriginal: "Загрузите файл или вставьте URL, чтобы увидеть оригинал",
    emptyAdapted: "Опишите потребности и создайте адаптированную версию",
    toggleLabel: "Адаптация:",
    toggleOn: "Вкл",
    toggleOff: "Выкл",
    reapplyBtn: "Повторить",
    footerLine1: "EMPI Agent | Траектория роста | 2026",
    footerLine2: "Создано Missvector",
    msgFileAttached: "Файл прикреплён",
    msgUrlFetched: "Материал загружен по ссылке",
    msgAdapting: "Адаптирую материал...",
    msgAdapted: "Готово! Смотрите во вкладке Адаптация.",
    msgNeedSource: "Сначала загрузите файл или вставьте URL.",
    msgNeedPrompt: "Опишите ваши потребности в поле ввода.",
    msgBackendNotReady: "Бэкенд не готов, подождите...",
    msgErrorFetch: "Не удалось загрузить содержимое по ссылке.",
    msgCopied: "Скопировано.",
    msgDownloaded: "Скачано.",
    msgReadapting: "Повторная адаптация...",
    alertInDevelopment: "Эта функция (загрузка по URL) в разработке. Пожалуйста, загрузите файл."
  }
};

let currentLang = (navigator.language || navigator.userLanguage || 'en').startsWith('ru') ? 'ru' : 'en';

function applyLang(lang) {
  currentLang = lang;
  document.querySelectorAll('[data-i18n]').forEach(function(el) {
    var key = el.getAttribute('data-i18n');
    if (translations[lang] && translations[lang][key]) {
      el.textContent = translations[lang][key];
    }
  });
  document.querySelectorAll('[data-i18n-placeholder]').forEach(function(el) {
    var key = el.getAttribute('data-i18n-placeholder');
    if (translations[lang] && translations[lang][key]) {
      el.setAttribute('placeholder', translations[lang][key]);
    }
  });
  document.documentElement.lang = lang;
  updateToggleText();
}

function t(key) {
  return (translations[currentLang] && translations[currentLang][key]) || key;
}

function updateToggleText() {
  var toggle = document.getElementById('adaptToggle');
  var label = document.getElementById('toggleStatusText');
  if (!toggle || !label) return;
  label.textContent = toggle.checked ? t('toggleOn') : t('toggleOff');
  label.style.color = toggle.checked ? '#5c9e6b' : '#8a8a8a';
}

var selectedFile = null;
var originalText = null;
var originalHtml = null;
var adaptedHtml = null;
var currentPrompt = '';

var fileInput = document.getElementById('fileInput');
var chatMessages = document.getElementById('chatMessages');
var chatPromptInput = document.getElementById('chatPromptInput');
var sendBtn = document.getElementById('sendBtn');
var attachBtn = document.getElementById('attachBtn');
var urlBtn = document.getElementById('urlBtn');
var urlInputRow = document.getElementById('urlInputRow');
var urlInput = document.getElementById('urlInput');
var fetchUrlBtn = document.getElementById('fetchUrlBtn');
var urlCancelBtn = document.getElementById('urlCancelBtn');

var previewTabs = document.querySelectorAll('.preview-tab');
var originalPane = document.getElementById('originalPane');
var adaptedPane = document.getElementById('adaptedPane');
var originalEmpty = document.getElementById('originalEmpty');
var originalTextPreview = document.getElementById('originalTextPreview');
var originalFrame = document.getElementById('originalFrame');
var adaptedEmpty = document.getElementById('adaptedEmpty');
var previewFrame = document.getElementById('previewFrame');
var adaptedBadge = document.getElementById('adaptedBadge');

var adaptToggle = document.getElementById('adaptToggle');
var adaptToggleBar = document.getElementById('adaptToggleBar');
var reapplyAdaptBtn = document.getElementById('reapplyAdaptBtn');
var statusBar = document.getElementById('statusBar');
var downloadBtn = document.getElementById('downloadBtn');
var copyBtn = document.getElementById('copyBtn');
var langToggle = document.getElementById('langToggle');

function addChatMsg(role) {
  var div = document.createElement('div');
  div.className = 'chat-msg ' + role;
  var avatar = document.createElement('div');
  avatar.className = 'avatar';
  avatar.innerHTML = role === 'user' ? '<i class="fas fa-user"></i>' : '<i class="fas fa-robot"></i>';
  var bubble = document.createElement('div');
  bubble.className = 'bubble';
  div.appendChild(avatar);
  div.appendChild(bubble);
  chatMessages.appendChild(div);
  chatMessages.scrollTop = chatMessages.scrollHeight;
  return bubble;
}

function addChatText(role, text) {
  var bubble = addChatMsg(role);
  bubble.textContent = text;
  return bubble;
}

function addFileBubble(file) {
  var bubble = addChatMsg('user');
  bubble.className += ' file-bubble';
  bubble.innerHTML =
    '<i class="fas fa-file-alt" style="color:#e0aaa0;"></i>' +
    '<span>' + file.name + ' (' + (file.size / 1024).toFixed(1) + ' KB)</span>' +
    '<i class="fas fa-times-circle remove-btn" data-action="remove-file"></i>';
  bubble.querySelector('.remove-btn').addEventListener('click', function(e) {
    e.stopPropagation();
    selectedFile = null;
    fileInput.value = '';
    bubble.remove();
    originalText = null;
    originalHtml = null;
    resetPreviews();
    addChatText('assistant', t('msgNeedSource'));
    statusBar.textContent = '';
    statusBar.className = 'status-bar';
  });
  return bubble;
}

function addUrlBubble(url) {
  var bubble = addChatMsg('user');
  bubble.className += ' file-bubble';
  var displayUrl = url.length > 40 ? url.substring(0, 40) + '...' : url;
  bubble.innerHTML =
    '<i class="fas fa-link" style="color:#e0aaa0;"></i>' +
    '<span>' + displayUrl + '</span>' +
    '<i class="fas fa-times-circle remove-btn" data-action="remove-url"></i>';
  bubble.querySelector('.remove-btn').addEventListener('click', function(e) {
    e.stopPropagation();
    originalText = null;
    originalHtml = null;
    resetPreviews();
    bubble.remove();
    addChatText('assistant', t('msgNeedSource'));
    statusBar.textContent = '';
    statusBar.className = 'status-bar';
  });
  return bubble;
}

function resetPreviews() {
  adaptedHtml = null;
  originalPane.classList.add('active');
  adaptedPane.classList.remove('active');
  previewTabs.forEach(function(t) { t.classList.remove('active'); });
  var t0 = document.querySelector('.preview-tab[data-pane="originalPane"]');
  if (t0) t0.classList.add('active');
  originalEmpty.style.display = '';
  originalTextPreview.style.display = 'none';
  originalFrame.style.display = 'none';
  originalFrame.srcdoc = '';
  adaptedEmpty.style.display = '';
  previewFrame.style.display = 'none';
  previewFrame.srcdoc = '';
  adaptedBadge.style.display = 'none';
  adaptToggleBar.classList.remove('show');
  adaptToggle.checked = true;
  updateToggleText();
}

function showOriginalText(text) {
  originalEmpty.style.display = 'none';
  originalFrame.style.display = 'none';
  originalTextPreview.style.display = 'block';
  originalTextPreview.textContent = text;
  originalPane.classList.add('active');
}

function showOriginalHtml(html) {
  originalEmpty.style.display = 'none';
  originalTextPreview.style.display = 'none';
  originalFrame.style.display = 'block';
  originalFrame.srcdoc = html;
  originalPane.classList.add('active');
}

function readFileAsText(file) {
  return new Promise(function(resolve, reject) {
    var reader = new FileReader();
    reader.onload = function() { resolve(reader.result); };
    reader.onerror = reject;
    reader.readAsText(file);
  });
}

attachBtn.addEventListener('click', function() {
  fileInput.click();
});

fileInput.addEventListener('change', function() {
  if (fileInput.files.length) {
    selectedFile = fileInput.files[0];
    addFileBubble(selectedFile);
    readFileAsText(selectedFile).then(function(text) {
      originalText = text;
      originalHtml = null;
      showOriginalText(text.substring(0, 5000));
      statusBar.textContent = t('msgFileAttached') + ': ' + selectedFile.name;
      statusBar.className = 'status-bar success';
    }).catch(function(e) {
      statusBar.textContent = e.message;
      statusBar.className = 'status-bar error';
    });
  }
});

var chatPanel = document.getElementById('chatPanel');
chatPanel.addEventListener('dragover', function(e) { e.preventDefault(); });
chatPanel.addEventListener('drop', function(e) {
  e.preventDefault();
  if (e.dataTransfer.files.length) {
    selectedFile = e.dataTransfer.files[0];
    var dt = new DataTransfer();
    dt.items.add(selectedFile);
    fileInput.files = dt.files;
    addFileBubble(selectedFile);
    readFileAsText(selectedFile).then(function(text) {
      originalText = text;
      originalHtml = null;
      showOriginalText(text.substring(0, 5000));
      statusBar.textContent = t('msgFileAttached') + ': ' + selectedFile.name;
      statusBar.className = 'status-bar success';
    }).catch(function(e) {
      statusBar.textContent = e.message;
      statusBar.className = 'status-bar error';
    });
  }
  var urlData = e.dataTransfer.getData('text/plain');
  if (urlData && (urlData.indexOf('http://') === 0 || urlData.indexOf('https://') === 0)) {
    handleUrlFetch(urlData);
  }
});

urlBtn.addEventListener('click', function() {
  urlInputRow.classList.toggle('show');
  if (urlInputRow.classList.contains('show')) urlInput.focus();
});

urlCancelBtn.addEventListener('click', function() {
  urlInputRow.classList.remove('show');
  urlInput.value = '';
});

fetchUrlBtn.addEventListener('click', function() {
  var url = urlInput.value.trim();
  if (!url) return;
  handleUrlFetch(url);
  urlInputRow.classList.remove('show');
  urlInput.value = '';
});

urlInput.addEventListener('keypress', function(e) {
  if (e.key === 'Enter') {
    var url = urlInput.value.trim();
    if (!url) return;
    handleUrlFetch(url);
    urlInputRow.classList.remove('show');
    urlInput.value = '';
  }
});

async function handleUrlFetch(url) {
  // Check if backend has fetchUrl method
  if (backend && typeof backend.fetchUrl === 'function') {
    addUrlBubble(url);
    statusBar.textContent = 'Fetching URL...';
    statusBar.className = 'status-bar';
    try {
      var content = await backend.fetchUrl(url);
      if (content && content.trim().charAt(0) === '<' && content.indexOf('<') !== -1) {
        originalHtml = content;
        originalText = stripHtml(content);
        showOriginalHtml(content);
      } else {
        originalHtml = null;
        originalText = content;
        showOriginalText(content);
      }
      addChatText('assistant', t('msgUrlFetched'));
      statusBar.textContent = t('msgUrlFetched');
      statusBar.className = 'status-bar success';
      selectedFile = null;
    } catch (e) {
      addChatText('assistant', t('msgErrorFetch') + ': ' + e.message);
      statusBar.textContent = t('msgErrorFetch');
      statusBar.className = 'status-bar error';
    }
  } else {
    // Show alert that feature is under development
    alert(t('alertInDevelopment'));
    addChatText('assistant', t('alertInDevelopment'));
  }
}

function stripHtml(html) {
  var tmp = document.createElement('div');
  tmp.innerHTML = html;
  return tmp.textContent || tmp.innerText || '';
}

async function doAdapt(promptText) {
  if (!originalText && !originalHtml) {
    addChatText('assistant', t('msgNeedSource'));
    statusBar.textContent = t('msgNeedSource');
    statusBar.className = 'status-bar error';
    return;
  }
  if (!promptText || !promptText.trim()) {
    addChatText('assistant', t('msgNeedPrompt'));
    statusBar.textContent = t('msgNeedPrompt');
    statusBar.className = 'status-bar error';
    return;
  }
  if (!backend) {
    currentPrompt = promptText.trim();
    addChatText('assistant', t('msgAdapting'));
    statusBar.textContent = t('msgAdapting');
    statusBar.className = 'status-bar';
    adaptToggleBar.classList.add('show');
    await new Promise(function(r) { setTimeout(r, 800); });
    var sourceText = originalText || stripHtml(originalHtml || '');
    adaptedHtml = generateDemoAdaptation(sourceText, currentPrompt);
    adaptedEmpty.style.display = 'none';
    previewFrame.style.display = 'block';
    previewFrame.srcdoc = adaptedHtml;
    adaptedBadge.style.display = 'inline';
    switchToPane('adaptedPane');
    addChatText('assistant', t('msgAdapted'));
    statusBar.textContent = t('msgAdapted');
    statusBar.className = 'status-bar success';
    return;
  }
  currentPrompt = promptText.trim();
  addChatText('assistant', t('msgAdapting'));
  statusBar.textContent = t('msgAdapting');
  statusBar.className = 'status-bar';
  adaptToggleBar.classList.add('show');
  try {
    var textToAdapt = originalText || stripHtml(originalHtml);
    var html = await backend.adapt(textToAdapt, currentPrompt);
    adaptedHtml = html;
    adaptedEmpty.style.display = 'none';
    previewFrame.style.display = 'block';
    previewFrame.srcdoc = html;
    adaptedBadge.style.display = 'inline';
    switchToPane('adaptedPane');
    addChatText('assistant', t('msgAdapted'));
    statusBar.textContent = t('msgAdapted');
    statusBar.className = 'status-bar success';
  } catch (e) {
    addChatText('assistant', 'Error: ' + e.message);
    statusBar.textContent = e.message;
    statusBar.className = 'status-bar error';
  }
}

function generateDemoAdaptation(text, prompt) {
  var pLower = prompt.toLowerCase();
  var style = 'font-size:17px; line-height:1.7;';
  if (pLower.indexOf('adhd') !== -1 || pLower.indexOf('сдвг') !== -1) {
    style = 'font-size:18px; line-height:1.8; max-width:700px; margin:0 auto;';
  } else if (pLower.indexOf('dyslexia') !== -1 || pLower.indexOf('дислекс') !== -1) {
    style = 'font-family: "OpenDyslexic", "Comic Sans MS", sans-serif; font-size:20px; line-height:2; word-spacing:4px;';
  } else if (pLower.indexOf('child') !== -1 || pLower.indexOf('дет') !== -1) {
    style = 'font-size:20px; line-height:1.9; color:#333;';
  }
  var paragraphs = text.split(/\n\n+/).filter(function(p) { return p.trim(); });
  var adapted = paragraphs.map(function(p) {
    var s = p.replace(/([.!?])\s+/g, '$1<br><br>');
    return '<p style="margin-bottom:16px;">' + s + '</p>';
  }).join('\n');
  return '<!DOCTYPE html>\n<html><head><meta charset="UTF-8"><style>\n' +
    'body { ' + style + ' padding:32px; background:#fffef9; color:#2c2c2c; }\n' +
    'h1,h2,h3 { color:#e0aaa0; margin-top:24px; }\n' +
    '.note { background:rgba(224,170,160,0.1); padding:12px 16px; border-left:3px solid #e0aaa0; margin:16px 0; border-radius:0 8px 8px 0; }\n' +
    '</style></head><body>\n' +
    '<h2>Adapted Content</h2>\n' +
    '<p style="color:#8a8a8a;font-size:0.85em;margin-bottom:24px;">Adaptation prompt: "' + prompt + '"</p>\n' +
    adapted + '\n</body></html>';
}

function handleSend() {
  var text = chatPromptInput.value.trim();
  if (!text) return;
  chatPromptInput.value = '';
  addChatText('user', text);
  doAdapt(text);
}

sendBtn.addEventListener('click', handleSend);

chatPromptInput.addEventListener('keypress', function(e) {
  if (e.key === 'Enter') handleSend();
});

document.querySelectorAll('.preset-chip').forEach(function(chip) {
  chip.addEventListener('click', function() {
    var preset = currentLang === 'ru'
      ? chip.getAttribute('data-prompt-ru')
      : chip.getAttribute('data-prompt-en');
    chatPromptInput.value = preset;
    addChatText('user', preset);
    doAdapt(preset);
  });
});

reapplyAdaptBtn.addEventListener('click', function() {
  if (!currentPrompt) return;
  addChatText('user', currentPrompt);
  statusBar.textContent = t('msgReadapting');
  statusBar.className = 'status-bar';
  doAdapt(currentPrompt);
});

adaptToggle.addEventListener('change', function() {
  updateToggleText();
  if (adaptToggle.checked && adaptedHtml) {
    switchToPane('adaptedPane');
  } else {
    switchToPane('originalPane');
  }
});

function switchToPane(paneId) {
  document.querySelectorAll('.preview-pane').forEach(function(p) { p.classList.remove('active'); });
  var pane = document.getElementById(paneId);
  if (pane) pane.classList.add('active');
  previewTabs.forEach(function(t) { t.classList.remove('active'); });
  var tab = document.querySelector('.preview-tab[data-pane="' + paneId + '"]');
  if (tab) tab.classList.add('active');
}

previewTabs.forEach(function(tab) {
  tab.addEventListener('click', function() {
    var paneId = tab.getAttribute('data-pane');
    switchToPane(paneId);
    adaptToggle.checked = (paneId === 'adaptedPane');
    updateToggleText();
  });
});

downloadBtn.addEventListener('click', function() {
  var activePane = document.querySelector('.preview-pane.active');
  var html = '';
  if (activePane && activePane.id === 'adaptedPane' && adaptedHtml) {
    html = adaptedHtml;
  } else if (activePane && activePane.id === 'originalPane' && originalHtml) {
    html = originalHtml;
  } else {
    html = originalText || '';
  }
  var blob = new Blob([html], { type: 'text/html' });
  var url = URL.createObjectURL(blob);
  var a = document.createElement('a');
  a.href = url;
  a.download = 'content.html';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  statusBar.textContent = t('msgDownloaded');
  statusBar.className = 'status-bar success';
  setTimeout(function() { statusBar.textContent = ''; statusBar.className = 'status-bar'; }, 2000);
});

copyBtn.addEventListener('click', function() {
  var activePane = document.querySelector('.preview-pane.active');
  var content = '';
  if (activePane && activePane.id === 'adaptedPane' && adaptedHtml) {
    content = adaptedHtml;
  } else if (activePane && activePane.id === 'originalPane' && originalHtml) {
    content = originalHtml;
  } else {
    content = originalText || '';
  }
  navigator.clipboard.writeText(content).then(function() {
    statusBar.textContent = t('msgCopied');
    statusBar.className = 'status-bar success';
    setTimeout(function() { statusBar.textContent = ''; statusBar.className = 'status-bar'; }, 2000);
  }).catch(function() {
    statusBar.textContent = 'Copy failed';
    statusBar.className = 'status-bar error';
  });
});

langToggle.addEventListener('click', function() {
  applyLang(currentLang === 'en' ? 'ru' : 'en');
});

var panelResizer = document.getElementById('panelResizer');
var startX, startWidth;
panelResizer.addEventListener('mousedown', function(e) {
  startX = e.clientX;
  startWidth = document.getElementById('chatPanel').offsetWidth;
  document.addEventListener('mousemove', resizeMove);
  document.addEventListener('mouseup', resizeStop);
  document.body.style.userSelect = 'none';
});
function resizeMove(e) {
  var chatPanelEl = document.getElementById('chatPanel');
  var w = startWidth + (e.clientX - startX);
  if (w > 220 && w < window.innerWidth - 300) chatPanelEl.style.width = w + 'px';
}
function resizeStop() {
  document.removeEventListener('mousemove', resizeMove);
  document.removeEventListener('mouseup', resizeStop);
  document.body.style.userSelect = '';
}

applyLang(currentLang);
