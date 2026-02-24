const menuButton = document.getElementById('menuButton');
const nav = document.getElementById('mainNav');
const links = nav.querySelectorAll('a');
const status = document.getElementById('menuStatus');
const menubalken = document.getElementById('menubalken');

function openMenu() {
    menuButton.classList.add('open');
    nav.classList.add('open');
    menubalken.classList.add('open');
    menuButton.setAttribute('aria-expanded', 'true');
    nav.setAttribute('aria-hidden', 'false');
    menubalken.setAttribute('aria-expanded', 'true');
    status.textContent = 'Menü geöffnet';
    links.length && links[0].focus();
}

function closeMenu() {
    menuButton.classList.remove('open');
    nav.classList.remove('open');
    menubalken.classList.remove('open');
    menuButton.setAttribute('aria-expanded', 'false');
    nav.setAttribute('aria-hidden', 'true');
    menubalken.setAttribute('aria-hidden', 'false');
    status.textContent = 'Menü geschlossen';
}

function toggleDarkMode() {
    document.body.classList.toggle('dark-mode');
    const savedTheme = localStorage.getItem('theme');
    if (savedTheme === 'dark') {
    }
}

menuButton.addEventListener('click', () => {
    menuButton.classList.contains('open') ? closeMenu() : openMenu();
});

nav.addEventListener('keydown', (e) => {
    if (e.key !== 'Tab') return;

    const first = links[0];
    const last = links[links.length - 1];

    if (e.shiftKey && document.activeElement === first) {
        e.preventDefault();
        last.focus();
    } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault();
        first.focus();
    }
});

document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && menuButton.classList.contains('open')) {
        closeMenu();
    }
});

document.addEventListener('keydown', (e) => {
    const active = document.activeElement;
    if (
        e.key.toLowerCase() === 'm' &&
        !['INPUT', 'TEXTAREA'].includes(active.tagName) &&
        !active.isContentEditable
    ) {
        menuButton.click();
    }
});

document.addEventListener('click', (e) => {
    if (!nav.contains(e.target) && !menuButton.contains(e.target)) {
        closeMenu();
    }
});
