/**
 * M5Stack Spannungsmesser - Main JavaScript
 * Enthält: Menü-Steuerung, Dark Mode Toggle
 */

// ===============================
// DOM Elemente auswählen
// ===============================
const menuButton = document.getElementById('menuButton');
const nav = document.getElementById('mainNav');
const links = nav.querySelectorAll('a');
const status = document.getElementById('menuStatus');
const menubalken = document.getElementById('menubalken');
const darkmodeToggle = document.getElementById('darkmode-toggle');

// ===============================
// Dark Mode Funktionen
// ===============================

/**
 * Initialisiert den Dark Mode beim Seitenladen
 * Liest den gespeicherten Status aus localStorage
 * - localStorage speichert Daten dauerhaft im Browser
 * - Funktioniert seitenübergreifend (alle Seiten teilen den gleichen localStorage)
 */
function initDarkMode() {
    const savedTheme = localStorage.getItem('theme');
    if (savedTheme === 'dark') {
        document.body.classList.add('dark-mode');
        darkmodeToggle.checked = true;
    }
}

/**
 * Toggle-Funktion für Dark Mode
 * - Schaltet CSS-Klasse auf body um
 * - Speichert Status in localStorage
 * - Aktualisiert den Schalter-Zustand
 */
function toggleDarkMode() {
    document.body.classList.toggle('dark-mode');
    const isDark = document.body.classList.contains('dark-mode');
    darkmodeToggle.checked = isDark;
    localStorage.setItem('theme', isDark ? 'dark' : 'light');
}

// ===============================
// Menü Funktionen
// ===============================

/**
 * Öffnet das Navigationsmenü
 * Fügt 'open' Klasse zu allen Elementen hinzu
 * Setzt Accessibility-Attribute (aria-expanded)
 */
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

/**
 * Schließt das Navigationsmenü
 * Entfernt 'open' Klasse von allen Elementen
 */
function closeMenu() {
    menuButton.classList.remove('open');
    nav.classList.remove('open');
    menubalken.classList.remove('open');
    menuButton.setAttribute('aria-expanded', 'false');
    nav.setAttribute('aria-hidden', 'true');
    menubalken.setAttribute('aria-hidden', 'false');
    status.textContent = 'Menü geschlossen';
}

// ===============================
// Event Listeners (Ereignisbehandlung)
// ===============================

// Initialisierung beim Laden
initDarkMode();

// Dark Mode: Reagiert auf Änderung des Schalters
darkmodeToggle.addEventListener('change', toggleDarkMode);

// Menü-Button: Öffnet/Schließt Menü beim Klick
menuButton.addEventListener('click', () => {
    menuButton.classList.contains('open') ? closeMenu() : openMenu();
});

// Navigation: Tastatur-Navigation (Tab-Shifting verhindern)
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

// Globale Tastatur: Escape schließt Menü
document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && menuButton.classList.contains('open')) {
        closeMenu();
    }
});

// Globale Tastatur: M-Taste öffnet Menü (außer in Input-Feldern)
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

// Klick außerhalb des Menüs schließt es
document.addEventListener('click', (e) => {
    if (!nav.contains(e.target) && !menuButton.contains(e.target)) {
        closeMenu();
    }
});
