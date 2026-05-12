// ============================================================================
// VX Studio Website - Interactive Scripts
// ============================================================================

// Smooth scroll for navigation links
document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function (e) {
        e.preventDefault();
        const target = document.querySelector(this.getAttribute('href'));

        if (target) {
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }

        if (navMenu && navMenu.classList.contains('open')) {
            navMenu.classList.remove('open');
            menuToggle?.setAttribute('aria-expanded', 'false');
        }
    });
});

// Mobile menu toggle
const menuToggle = document.querySelector('.menu-toggle');
const navMenu = document.querySelector('.nav-panel');

if (menuToggle && navMenu) {
    menuToggle.addEventListener('click', () => {
        const isOpen = navMenu.classList.toggle('open');
        menuToggle.setAttribute('aria-expanded', String(isOpen));
    });

    window.addEventListener('resize', () => {
        if (window.innerWidth > 768) {
            navMenu.classList.remove('open');
            menuToggle.setAttribute('aria-expanded', 'false');
        }
    });
}

// Product card animation on scroll
const observerOptions = {
    threshold: 0.1,
    rootMargin: '0px 0px -100px 0px'
};

const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
        if (entry.isIntersecting) {
            entry.target.style.animation = 'fadeInUp 0.6s ease-out forwards';
            observer.unobserve(entry.target);
        }
    });
}, observerOptions);

document.querySelectorAll('.product-card, .detail-card, .feature, .tech-item, .doc-link, .notice-box').forEach(el => {
    observer.observe(el);
});

// Add fade-in animation
const style = document.createElement('style');
style.textContent = `
    @keyframes fadeInUp {
        from {
            opacity: 0;
            transform: translateY(30px);
        }
        to {
            opacity: 1;
            transform: translateY(0);
        }
    }
`;
document.head.appendChild(style);

// Analytics & tracking (minimal - respect privacy)
function trackEvent(eventName, eventData = {}) {
    // Only track if user has not opted out
    if (typeof window !== 'undefined' && window.navigator.doNotTrack !== '1') {
        // Placeholder for analytics integration
        console.log(`Event: ${eventName}`, eventData);
    }
}

// Track downloads
document.querySelectorAll('a[href*="downloads"]').forEach(link => {
    link.addEventListener('click', () => {
        trackEvent('download_click');
    });
});

// Track external links
document.querySelectorAll('a[target="_blank"]').forEach(link => {
    link.addEventListener('click', () => {
        trackEvent('external_link', { url: link.href });
    });
});

console.log('%cVX Studio', 'font-size: 24px; color: #0066FF; font-weight: bold;');
console.log('%cProfessional Audio Processing Plugins', 'font-size: 14px; color: #666;');
console.log('Learn more: https://vxstudio.marczewski.me.uk');
