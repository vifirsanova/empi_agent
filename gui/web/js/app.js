// ============================================================
// JWT AUTH
// ============================================================

let authToken = null;
let isAuthenticated = false;
let isLoading = false;
let pendingResolve = null;
let pendingReject = null;
let timeoutId = null;

function getAuthHeaders() {
    return {
        'Authorization': 'Bearer ' + authToken,
        'Content-Type': 'application/json'
    };
}

function checkStoredToken() {
    const token = localStorage.getItem('empi_token');
    const expires = parseInt(localStorage.getItem('empi_token_expires') || '0');
    
    if (token && expires > Date.now()) {
        authToken = token;
        isAuthenticated = true;
        return true;
    }
    
    localStorage.removeItem('empi_token');
    localStorage.removeItem('empi_token_expires');
    return false;
}

// ============================================================
// LOGIN LOGIC
// ============================================================

async function loginWithCode(code) {
    try {
        const response = await fetch('/api/auth', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ code: code })
        });
        
        const data = await response.json();
        
        if (response.status === 200 && data.success) {
            authToken = data.token;
            isAuthenticated = true;
            
            localStorage.setItem('empi_token', authToken);
            localStorage.setItem('empi_token_expires', Date.now() + data.expires_in * 1000);
            
            return { success: true };
        } else {
            return { 
                success: false, 
                error: data.error || 'Invalid code',
                remaining: data.remaining_attempts,
                blocked: data.is_blocked,
                blockedUntil: data.blocked_until
            };
        }
    } catch (e) {
        return { success: false, error: 'Connection error: ' + e.message };
    }
}

// ============================================================
// POLLING RESULT
// ============================================================

function pollResult(taskId) {
    return new Promise((resolve, reject) => {
        let attempts = 0;
        const maxAttempts = 120;
        
        function check() {
            attempts++;
            fetch('/api/result/' + taskId, {
                headers: getAuthHeaders()
            })
                .then(response => {
                    if (!response.ok) {
                        throw new Error('HTTP ' + response.status);
                    }
                    return response.json();
                })
                .then(data => {
                    if (data.status === 'completed') {
                        if (backend.adaptationComplete && backend.adaptationComplete._callback) {
                            backend.adaptationComplete._callback(data.html);
                        }
                        resolve(data.html);
                    } else if (data.status === 'processing') {
                        if (attempts < maxAttempts) {
                            setTimeout(check, 2000);
                        } else {
                            reject(new Error('Timeout waiting for result'));
                        }
                    } else {
                        reject(new Error('Task failed: ' + JSON.stringify(data)));
                    }
                })
                .catch(reject);
        }
        check();
    });
}

// ============================================================
// BACKEND
// ============================================================

const backend = {
    adapt: function(text, prompt) {
        return fetch('/api/adapt', {
            method: 'POST',
            headers: getAuthHeaders(),
            body: JSON.stringify({ text: text, prompt: prompt })
        })
        .then(response => {
            if (response.status === 401) {
                throw new Error('Session expired, please login again');
            }
            if (!response.ok) {
                throw new Error('Server error: ' + response.status);
            }
            return response.json();
        })
        .then(data => {
            if (data.status === 'processing') {
                return pollResult(data.task_id);
            }
            throw new Error('Unexpected response');
        });
    },
    
    fetchUrl: function(url) {
        return fetch('/api/fetch', {
            method: 'POST',
            headers: getAuthHeaders(),
            body: JSON.stringify({ url: url })
        })
        .then(response => {
            if (response.status === 401) {
                throw new Error('Session expired, please login again');
            }
            return response.json();
        })
        .then(data => data.content);
    },
    
    parseDocumentFromContent: function(filename, base64Content) {
        return fetch('/api/parse', {
            method: 'POST',
            headers: getAuthHeaders(),
            body: JSON.stringify({ 
                filename: filename, 
                content: base64Content 
            })
        })
        .then(response => {
            if (response.status === 401) {
                throw new Error('Session expired, please login again');
            }
            return response.json();
        })
        .then(data => data.content);
    },
    
    openExternalUrl: function(url) {
        window.open(url, '_blank', 'noopener,noreferrer');
        return true;
    }
};

backend.adaptationComplete = {
    _callback: null,
    connect: function(callback) {
        this._callback = callback;
    }
};

function adaptAsync(text, prompt) {
    return new Promise((resolve, reject) => {
        pendingResolve = resolve;
        pendingReject = reject;
        
        timeoutId = setTimeout(() => {
            if (pendingReject) {
                pendingReject(new Error('Timeout waiting for adaptation (5 minutes)'));
                pendingResolve = null;
                pendingReject = null;
            }
            hideCog();
        }, 300000);
        
        backend.adapt(text, prompt)
            .then(result => {
                if (pendingResolve) {
                    pendingResolve(result);
                    pendingResolve = null;
                    pendingReject = null;
                    if (timeoutId) clearTimeout(timeoutId);
                    timeoutId = null;
                }
                hideCog();
            })
            .catch(error => {
                if (pendingReject) {
                    pendingReject(error);
                    pendingResolve = null;
                    pendingReject = null;
                    if (timeoutId) clearTimeout(timeoutId);
                    timeoutId = null;
                }
                hideCog();
            });
    });
}

// ============================================================
// UI HELPERS
// ============================================================

function showCog() {
    const adaptedPane = document.getElementById('adaptedPane');
    if (!adaptedPane) return;
    
    const existingCog = document.getElementById('loadingCog');
    if (existingCog) existingCog.remove();
    
    const cogDiv = document.createElement('div');
    cogDiv.id = 'loadingCog';
    cogDiv.style.cssText = `
        position: absolute;
        top: 50%;
        left: 50%;
        transform: translate(-50%, -50%);
        z-index: 1000;
        background: rgba(255,255,255,0.95);
        padding: 20px;
        border-radius: 12px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.15);
        text-align: center;
    `;
    cogDiv.innerHTML = `
        <i class="fas fa-cog fa-spin" style="font-size: 48px; color: #e0aaa0;"></i>
        <div style="margin-top: 8px; font-size: 12px; color: #666;">Generating...</div>
    `;
    
    if (getComputedStyle(adaptedPane).position === 'static') {
        adaptedPane.style.position = 'relative';
    }
    adaptedPane.appendChild(cogDiv);
}

function hideCog() {
    const cog = document.getElementById('loadingCog');
    if (cog) cog.remove();
}

function cleanHtmlResponse(html) {
    if (!html) return html;
    let cleaned = html.replace(/^```html\s*\n?/i, '');
    cleaned = cleaned.replace(/\n?```\s*$/i, '');
    return cleaned;
}

function sanitizeHtml(html) {
    if (!html) return html;
    
    const temp = document.createElement('div');
    temp.innerHTML = html;
    
    temp.querySelectorAll('script, iframe, object, embed, form, input, button').forEach(el => el.remove());
    
    temp.querySelectorAll('a').forEach(link => {
        const href = link.getAttribute('href');
        if (href) {
            if (href.startsWith('file://') || href.toLowerCase().startsWith('javascript:')) {
                link.removeAttribute('href');
                link.style.cursor = 'not-allowed';
                link.style.opacity = '0.6';
                link.style.textDecoration = 'line-through';
                link.title = 'Access denied for security reasons';
            }
            else if (href.startsWith('http://') || href.startsWith('https://')) {
                link.setAttribute('target', '_blank');
                link.setAttribute('rel', 'noopener noreferrer');
                link.addEventListener('click', (e) => {
                    e.preventDefault();
                    if (backend && backend.openExternalUrl) {
                        backend.openExternalUrl(href);
                    } else if (window.parent && window.parent.backend) {
                        window.parent.backend.openExternalUrl(href);
                    } else {
                        window.open(href, '_blank', 'noopener,noreferrer');
                    }
                });
            }
            else if (href.startsWith('#')) {
                // keep internal navigation
            }
            else {
                link.removeAttribute('href');
                link.style.cursor = 'not-allowed';
                link.style.opacity = '0.6';
                link.title = 'Only HTTP/HTTPS links are allowed';
            }
        }
    });
    
    temp.querySelectorAll('*').forEach(el => {
        const attributes = el.attributes;
        for (let i = attributes.length - 1; i >= 0; i--) {
            const attrName = attributes[i].name;
            if (attrName.toLowerCase().startsWith('on')) {
                el.removeAttribute(attrName);
            }
        }
    });
    
    return temp.innerHTML;
}

function showAdaptedHtml(html) {
    const safeHtml = sanitizeHtml(html);
    const framedHtml = `<!DOCTYPE html>
    <html>
    <head>
        <meta charset="UTF-8">
        <meta http-equiv="Content-Security-Policy" content="default-src 'self' https: data:; script-src 'none'; style-src 'unsafe-inline' https:; img-src https: data:;">
        <meta name="referrer" content="no-referrer">
        <base target="_blank">
        <style>
            body {
                font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
                line-height: 1.6;
                padding: 20px;
                max-width: 900px;
                margin: 0 auto;
                background: #fffef9;
                color: #2c2c2c;
            }
            a {
                color: #e0aaa0;
                text-decoration: none;
                border-bottom: 1px solid rgba(224,170,160,0.3);
            }
            a:hover {
                border-bottom-color: #e0aaa0;
            }
            a[href^="http"]::after {
                content: " " + String.fromCharCode(128279);
                font-size: 0.8em;
                opacity: 0.6;
            }
            img {
                max-width: 100%;
                height: auto;
            }
            pre, code {
                background: #f5f5f5;
                padding: 2px 6px;
                border-radius: 4px;
                overflow-x: auto;
            }
            blockquote {
                border-left: 3px solid #e0aaa0;
                margin: 16px 0;
                padding-left: 16px;
                color: #666;
            }
            .note, .warning, .info {
                padding: 12px 16px;
                border-radius: 8px;
                margin: 16px 0;
            }
            .note {
                background: rgba(224,170,160,0.1);
                border-left: 3px solid #e0aaa0;
            }
        </style>
    </head>
    <body>
        ${safeHtml}
    </body>
    </html>`;
    
    const previewFrame = document.getElementById('previewFrame');
    const adaptedEmpty = document.getElementById('adaptedEmpty');
    const adaptedBadge = document.getElementById('adaptedBadge');
    
    previewFrame.srcdoc = framedHtml;
    adaptedEmpty.style.display = 'none';
    previewFrame.style.display = 'block';
    adaptedBadge.style.display = 'inline';
}

function showOriginalText(text) {
    const originalEmpty = document.getElementById('originalEmpty');
    const originalFrame = document.getElementById('originalFrame');
    const originalTextPreview = document.getElementById('originalTextPreview');
    const originalPane = document.getElementById('originalPane');
    
    originalEmpty.style.display = 'none';
    originalFrame.style.display = 'none';
    originalTextPreview.style.display = 'block';
    originalTextPreview.textContent = text;
    originalPane.classList.add('active');
}

function showOriginalHtml(html) {
    const safeHtml = sanitizeHtml(html);
    const originalEmpty = document.getElementById('originalEmpty');
    const originalTextPreview = document.getElementById('originalTextPreview');
    const originalFrame = document.getElementById('originalFrame');
    const originalPane = document.getElementById('originalPane');
    
    originalEmpty.style.display = 'none';
    originalTextPreview.style.display = 'none';
    originalFrame.style.display = 'block';
    originalFrame.srcdoc = safeHtml;
    originalPane.classList.add('active');
}

function resetPreviews() {
    const adaptedHtml = null;
    const originalPane = document.getElementById('originalPane');
    const adaptedPane = document.getElementById('adaptedPane');
    const previewTabs = document.querySelectorAll('.preview-tab');
    const originalEmpty = document.getElementById('originalEmpty');
    const originalTextPreview = document.getElementById('originalTextPreview');
    const originalFrame = document.getElementById('originalFrame');
    const adaptedEmpty = document.getElementById('adaptedEmpty');
    const previewFrame = document.getElementById('previewFrame');
    const adaptedBadge = document.getElementById('adaptedBadge');
    const adaptToggleBar = document.getElementById('adaptToggleBar');
    const adaptToggle = document.getElementById('adaptToggle');
    
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

// ============================================================
// PARSE DOCUMENT
// ============================================================

async function parseDocument(file) {
    if (!backend || typeof backend.parseDocumentFromContent !== 'function') {
        throw new Error('Document parsing not available');
    }
    
    return new Promise((resolve, reject) => {
        const timeoutId = setTimeout(() => {
            reject(new Error('Document parsing timeout'));
        }, 30000);
        
        const reader = new FileReader();
        reader.onload = function(e) {
            try {
                const arrayBuffer = e.target.result;
                const uint8Array = new Uint8Array(arrayBuffer);
                let binary = '';
                for (let i = 0; i < uint8Array.length; i++) {
                    binary += String.fromCharCode(uint8Array[i]);
                }
                const base64 = btoa(binary);
                
                backend.parseDocumentFromContent(file.name, base64)
                    .then(result => {
                        clearTimeout(timeoutId);
                        resolve(result);
                    })
                    .catch(error => {
                        clearTimeout(timeoutId);
                        reject(error);
                    });
            } catch (error) {
                clearTimeout(timeoutId);
                reject(error);
            }
        };
        reader.onerror = function() {
            clearTimeout(timeoutId);
            reject(new Error('Failed to read file'));
        };
        reader.readAsArrayBuffer(file);
    });
}

// ============================================================
// CANVAS BACKGROUND
// ============================================================

const canvas = document.getElementById("canvas-bg");
const ctx = canvas.getContext("2d");
let w, h, particles = [];

function resizeCanvas() {
    w = window.innerWidth;
    h = window.innerHeight;
    canvas.width = w;
    canvas.height = h;
    particles = [];
    const count = Math.floor(Math.min(w, h) / 22);
    for (let i = 0; i < count; i++) {
        const r = Math.random() * 90 + 30;
        particles.push({
            x: Math.random() * w,
            y: Math.random() * h,
            dx: (Math.random() - 0.5) * 0.12,
            dy: (Math.random() - 0.5) * 0.12,
            r: r,
            alpha: Math.random() * 0.1 + 0.03
        });
    }
}

function drawCanvas() {
    ctx.clearRect(0, 0, w, h);
    particles.forEach(function(p) {
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(224, 170, 160, ' + p.alpha + ')';
        ctx.fill();
        p.x += p.dx;
        p.y += p.dy;
        if (p.x + p.r > w || p.x - p.r < 0) p.dx *= -1;
        if (p.y + p.r > h || p.y - p.r < 0) p.dy *= -1;
    });
    requestAnimationFrame(drawCanvas);
}

resizeCanvas();
window.addEventListener('resize', resizeCanvas);
drawCanvas();

// ============================================================
// TRANSLATIONS
// ============================================================

const translations = {
    en: {
        title: "EMPI Agent",
        subtitle: "Adaptive learning for everyone",
        langLabel: "RU",
        downloadBtn: "Download",
        copyBtn: "Copy",
        welcomeMsg: "Upload a file (PDF, DOCX, TXT, HTML), paste a URL, or type your needs. I'll adapt content for you.",
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
        msgCopied: "Copied!",
        msgDownloaded: "Downloaded!",
        msgReadapting: "Re-adapting...",
        msgParsing: "Parsing document...",
        msgParseError: "Failed to parse document",
        supportedFormats: "Supported: PDF, DOCX, DOC, TXT, HTML, MD"
    },
    ru: {
        title: "EMPI Agent",
        subtitle: "Adaptive learning for everyone",
        langLabel: "EN",
        downloadBtn: "Download",
        copyBtn: "Copy",
        welcomeMsg: "Upload a file (PDF, DOCX, TXT, HTML), paste a URL, or type your needs. I'll adapt content for you.",
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
        msgCopied: "Copied!",
        msgDownloaded: "Downloaded!",
        msgReadapting: "Re-adapting...",
        msgParsing: "Parsing document...",
        msgParseError: "Failed to parse document",
        supportedFormats: "Supported: PDF, DOCX, DOC, TXT, HTML, MD"
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

// ============================================================
// DOM REFERENCES
// ============================================================

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

// ============================================================
// CHAT FUNCTIONS
// ============================================================

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

// ============================================================
// LOGIN HANDLER
// ============================================================

const loginScreen = document.getElementById('loginScreen');
const container = document.querySelector('.container');
const tokenInput = document.getElementById('tokenInput');
const loginBtn = document.getElementById('loginBtn');
const loginError = document.getElementById('loginError');
const loginAttempts = document.getElementById('loginAttempts');

let attempts = 0;
const maxAttempts = 3;

async function handleLogin() {
    const code = tokenInput.value.trim();
    
    if (!code) {
        showLoginError('Enter access code');
        return;
    }
    
    loginBtn.disabled = true;
    loginBtn.textContent = 'Checking...';
    hideLoginError();
    
    try {
        const result = await loginWithCode(code);
        
        if (result.success) {
            loginScreen.style.display = 'none';
            container.style.display = 'flex';
            applyLang(currentLang);
            return;
        }
        
        if (result.blocked) {
            const blockedUntil = new Date(result.blockedUntil * 1000);
            showLoginError(
                'Too many attempts. IP blocked until ' + blockedUntil.toLocaleTimeString(),
                true
            );
            loginBtn.disabled = true;
            loginBtn.textContent = 'Blocked';
            return;
        }
        
        attempts++;
        const remaining = maxAttempts - attempts;
        
        if (remaining > 0) {
            showLoginError('Invalid code. Attempts remaining: ' + remaining);
            loginAttempts.textContent = 'Attempts: ' + attempts + '/' + maxAttempts;
        } else {
            showLoginError('Maximum attempts exceeded. Please wait 5 minutes.');
            loginAttempts.textContent = 'Blocked for 5 minutes';
            loginBtn.disabled = true;
            loginBtn.textContent = 'Blocked';
            
            setTimeout(function() {
                attempts = 0;
                loginBtn.disabled = false;
                loginBtn.textContent = 'Login';
                loginAttempts.textContent = '';
                hideLoginError();
                tokenInput.value = '';
                tokenInput.focus();
            }, 300000);
        }
        
        tokenInput.value = '';
        tokenInput.focus();
        
    } catch (e) {
        showLoginError('Connection error');
    } finally {
        if (!loginBtn.disabled) {
            loginBtn.disabled = false;
            loginBtn.textContent = 'Login';
        }
    }
}

function showLoginError(message, isBlocked) {
    loginError.textContent = message;
    loginError.style.display = 'block';
    if (isBlocked) {
        loginError.style.color = '#e53e3e';
        loginError.style.fontWeight = 'bold';
    } else {
        loginError.style.color = '#e53e3e';
        loginError.style.fontWeight = 'normal';
    }
}

function hideLoginError() {
    loginError.style.display = 'none';
}

// ============================================================
// EVENT LISTENERS
// ============================================================

loginBtn.addEventListener('click', handleLogin);

tokenInput.addEventListener('keypress', function(e) {
    if (e.key === 'Enter') {
        handleLogin();
    }
});

tokenInput.addEventListener('focus', function() {
    this.select();
});

attachBtn.addEventListener('click', function() {
    fileInput.click();
});

fileInput.addEventListener('change', async function() {
    if (fileInput.files.length) {
        selectedFile = fileInput.files[0];
        addFileBubble(selectedFile);
        
        showCog();
        statusBar.textContent = t('msgParsing');
        statusBar.className = 'status-bar';
        
        try {
            const extractedText = await parseDocument(selectedFile);
            hideCog();
            
            if (extractedText.startsWith('Error:') || extractedText.startsWith('Warning:')) {
                addChatText('assistant', extractedText);
                statusBar.textContent = extractedText;
                statusBar.className = 'status-bar error';
                return;
            }
            
            originalText = extractedText;
            originalHtml = null;
            showOriginalText(extractedText.substring(0, 10000) + (extractedText.length > 10000 ? '\n\n... (truncated)' : ''));
            
            statusBar.textContent = t('msgFileAttached') + ': ' + selectedFile.name;
            statusBar.className = 'status-bar success';
            
            if (chatPromptInput.value.trim()) {
                setTimeout(function() {
                    doAdapt(chatPromptInput.value.trim());
                }, 500);
            }
            
        } catch (e) {
            hideCog();
            addChatText('assistant', t('msgParseError') + ': ' + e.message);
            statusBar.textContent = t('msgParseError');
            statusBar.className = 'status-bar error';
        }
    }
});

var chatPanel = document.getElementById('chatPanel');
chatPanel.addEventListener('dragover', function(e) { e.preventDefault(); });
chatPanel.addEventListener('drop', async function(e) {
    e.preventDefault();
    if (e.dataTransfer.files.length) {
        selectedFile = e.dataTransfer.files[0];
        var dt = new DataTransfer();
        dt.items.add(selectedFile);
        fileInput.files = dt.files;
        addFileBubble(selectedFile);
        
        showCog();
        statusBar.textContent = t('msgParsing');
        
        try {
            const extractedText = await parseDocument(selectedFile);
            hideCog();
            
            if (extractedText.startsWith('Error:')) {
                addChatText('assistant', extractedText);
                statusBar.textContent = extractedText;
                statusBar.className = 'status-bar error';
                return;
            }
            
            originalText = extractedText;
            originalHtml = null;
            showOriginalText(extractedText.substring(0, 10000));
            statusBar.textContent = t('msgFileAttached') + ': ' + selectedFile.name;
            statusBar.className = 'status-bar success';
        } catch (e) {
            hideCog();
            addChatText('assistant', t('msgParseError') + ': ' + e.message);
        }
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

// ============================================================
// URL FETCH
// ============================================================

async function handleUrlFetch(url) {
    if (!backend || typeof backend.fetchUrl !== 'function') {
        addChatText('assistant', 'URL fetching not available');
        return;
    }
    
    let validatedUrl = url.trim();
    if (!validatedUrl.startsWith('http://') && !validatedUrl.startsWith('https://')) {
        validatedUrl = 'https://' + validatedUrl;
    }
    
    try {
        new URL(validatedUrl);
    } catch(e) {
        addChatText('assistant', 'Invalid URL format');
        return;
    }
    
    addUrlBubble(validatedUrl);
    statusBar.textContent = 'Fetching URL...';
    statusBar.className = 'status-bar';
    showCog();
    
    try {
        const content = await backend.fetchUrl(validatedUrl);
        hideCog();
        
        if (content.startsWith('Error:')) {
            addChatText('assistant', t('msgErrorFetch') + ': ' + content);
            statusBar.textContent = content;
            statusBar.className = 'status-bar error';
            return;
        }
        
        const isHtml = content.trim().startsWith('<') || 
                      content.toLowerCase().includes('<!doctype') ||
                      (content.includes('<html') && content.includes('</html>'));
        
        if (isHtml) {
            originalHtml = content;
            originalText = stripHtml(content);
            showOriginalHtml(sanitizeHtml(content));
        } else {
            originalHtml = null;
            originalText = content;
            showOriginalText(content.substring(0, 10000));
        }
        
        addChatText('assistant', t('msgUrlFetched'));
        statusBar.textContent = t('msgUrlFetched');
        statusBar.className = 'status-bar success';
        selectedFile = null;
        
        if (chatPromptInput.value.trim()) {
            setTimeout(function() {
                doAdapt(chatPromptInput.value.trim());
            }, 500);
        }
        
    } catch(e) {
        hideCog();
        addChatText('assistant', t('msgErrorFetch') + ': ' + e.message);
        statusBar.textContent = t('msgErrorFetch');
        statusBar.className = 'status-bar error';
    }
}

function stripHtml(html) {
    var tmp = document.createElement('div');
    tmp.innerHTML = html;
    return tmp.textContent || tmp.innerText || '';
}

// ============================================================
// ADAPT
// ============================================================

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
    
    if (isLoading) return;
    isLoading = true;
    
    currentPrompt = promptText.trim();
    addChatText('assistant', t('msgAdapting'));
    statusBar.textContent = t('msgAdapting');
    statusBar.className = 'status-bar';
    adaptToggleBar.classList.add('show');
    
    showCog();
    
    try {
        var textToAdapt = originalText || stripHtml(originalHtml);
        var html;
        
        if (backend) {
            html = await adaptAsync(textToAdapt, currentPrompt);
        } else {
            await new Promise(function(r) { setTimeout(r, 800); });
            html = generateDemoAdaptation(textToAdapt, currentPrompt);
        }
        
        html = cleanHtmlResponse(html);
        adaptedHtml = html;
        showAdaptedHtml(html);
        
        switchToPane('adaptedPane');
        addChatText('assistant', t('msgAdapted'));
        statusBar.textContent = t('msgAdapted');
        statusBar.className = 'status-bar success';
    } catch (e) {
        addChatText('assistant', 'Error: ' + e.message);
        statusBar.textContent = e.message;
        statusBar.className = 'status-bar error';
    } finally {
        hideCog();
        isLoading = false;
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

reapplyAdaptBtn.addEventListener('click', async function() {
    if (!currentPrompt) return;
    addChatText('user', currentPrompt);
    statusBar.textContent = t('msgReadapting');
    statusBar.className = 'status-bar';
    await doAdapt(currentPrompt);
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
    var content = '';
    if (activePane && activePane.id === 'adaptedPane' && adaptedHtml) {
        content = adaptedHtml;
    } else if (activePane && activePane.id === 'originalPane' && originalHtml) {
        content = originalHtml;
    } else {
        content = originalText || '';
    }
    
    var blob = new Blob([content], { type: 'text/html' });
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
    
    if (content.includes('<') && content.includes('>')) {
        const tempDiv = document.createElement('div');
        tempDiv.innerHTML = content;
        content = tempDiv.textContent || tempDiv.innerText;
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

var supportedFormatsHint = document.createElement('div');
supportedFormatsHint.className = 'supported-formats-hint';
supportedFormatsHint.style.cssText = 'font-size: 11px; color: #999; margin-top: 8px; text-align: center;';
supportedFormatsHint.textContent = t('supportedFormats');
var chatInputArea = document.querySelector('.chat-input-area');
if (chatInputArea) chatInputArea.appendChild(supportedFormatsHint);

// ============================================================
// INIT
// ============================================================

if (checkStoredToken()) {
    loginScreen.style.display = 'none';
    container.style.display = 'flex';
    applyLang(currentLang);
} else {
    loginScreen.style.display = 'flex';
    container.style.display = 'none';
    tokenInput.focus();
}
