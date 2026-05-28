let backend = null;

new QWebChannel(qt.webChannelTransport, function(channel) {
    backend = channel.objects.backend;
});

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
    uploadTitle: "Drop your text here or click to browse",
    uploadHint: ".txt, .pdf, .docx",
    promptLabel: "Your needs (optional)",
    promptPlaceholder: "e.g. I have ADHD, use short paragraphs and clear headings...",
    adaptBtn: "Adapt & Render",
    statusSelectFile: "Please select a file first",
    statusAdapting: "Adapting...",
    statusDone: "Done",
    footerLine1: "EMPI Agent | Trajectory of Growth | 2026",
    footerLine2: "Made by Missvector"
  },
  ru: {
    title: "EMPI Агент",
    subtitle: "Адаптивное обучение для каждого",
    langLabel: "EN",
    uploadTitle: "Перетащите файл сюда или нажмите для выбора",
    uploadHint: ".txt, .pdf, .docx",
    promptLabel: "Ваши потребности (опционально)",
    promptPlaceholder: "Например: у меня дислексия, используйте крупный шрифт и короткие абзацы...",
    adaptBtn: "Адаптировать",
    statusSelectFile: "Сначала выберите файл",
    statusAdapting: "Адаптация...",
    statusDone: "Готово",
    footerLine1: "EMPI Агент | Траектория роста | 2026",
    footerLine2: "Missvector"
  }
};

let currentLang = (navigator.language || navigator.userLanguage || 'en').startsWith('ru') ? 'ru' : 'en';

function applyLang(lang) {
  currentLang = lang;
  document.querySelectorAll('[data-i18n]').forEach(el => {
    const key = el.getAttribute('data-i18n');
    if (translations[lang]?.[key]) el.textContent = translations[lang][key];
  });
  document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
    const key = el.getAttribute('data-i18n-placeholder');
    if (translations[lang]?.[key]) el.setAttribute('placeholder', translations[lang][key]);
  });
  document.documentElement.lang = lang;
}

function t(key) {
  return translations[currentLang]?.[key] || key;
}

document.getElementById('langToggle').addEventListener('click', () => {
  applyLang(currentLang === 'en' ? 'ru' : 'en');
});
applyLang(currentLang);

const uploadArea = document.getElementById('uploadArea');
const fileInput = document.getElementById('fileInput');
const fileInfo = document.getElementById('fileInfo');
let selectedFile = null;

uploadArea.addEventListener('click', () => fileInput.click());
uploadArea.addEventListener('dragover', e => { e.preventDefault(); uploadArea.classList.add('drag-over'); });
uploadArea.addEventListener('dragleave', () => uploadArea.classList.remove('drag-over'));
uploadArea.addEventListener('drop', e => {
  e.preventDefault();
  uploadArea.classList.remove('drag-over');
  if (e.dataTransfer.files.length) {
    selectedFile = e.dataTransfer.files[0];
    showFileInfo();
  }
});
fileInput.addEventListener('change', () => {
  if (fileInput.files.length) {
    selectedFile = fileInput.files[0];
    showFileInfo();
  }
});

function showFileInfo() {
  if (!selectedFile) return;
  fileInfo.innerHTML = `
    <div class="file-info">
      <i class="fas fa-file-alt"></i>
      <span>${selectedFile.name} (${(selectedFile.size / 1024).toFixed(1)} KB)</span>
      <i class="fas fa-times-circle remove-file" id="removeFile"></i>
    </div>`;
  uploadArea.style.display = 'none';
  document.getElementById('removeFile').addEventListener('click', () => {
    selectedFile = null;
    fileInfo.innerHTML = '';
    uploadArea.style.display = '';
    fileInput.value = '';
  });
}

const adaptBtn = document.getElementById('adaptBtn');
const statusEl = document.getElementById('status');
const previewArea = document.getElementById('previewArea');
const previewFrame = document.getElementById('previewFrame');
const promptInput = document.getElementById('promptInput');

function readFileAsText(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result);
    reader.onerror = reject;
    reader.readAsText(file);
  });
}

adaptBtn.addEventListener('click', async () => {
  if (!selectedFile) {
    statusEl.textContent = t('statusSelectFile');
    statusEl.className = 'status error';
    return;
  }

  if (!backend) {
    statusEl.textContent = 'Backend not ready, please wait...';
    statusEl.className = 'status error';
    return;
  }

  adaptBtn.disabled = true;
  statusEl.textContent = t('statusAdapting');
  statusEl.className = 'status';
  previewArea.classList.remove('active');

  try {
    const text = await readFileAsText(selectedFile);
    const prompt = promptInput.value.trim();

    const html = await backend.adapt(text, prompt);
    previewFrame.srcdoc = html;
    previewArea.classList.add('active');
    statusEl.textContent = t('statusDone');
    statusEl.className = 'status success';

  } catch (e) {
    statusEl.textContent = e.message;
    statusEl.className = 'status error';
  }

  adaptBtn.disabled = false;
});
