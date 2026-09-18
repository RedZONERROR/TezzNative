(() => {
    const qs = (selector, root = document) => root.querySelector(selector);
    const qsa = (selector, root = document) => Array.from(root.querySelectorAll(selector));

    const normalizePath = (value) => {
        const clean = (value || '').toLowerCase().replace(/\/+/g, '/').replace(/^\/+|\/+$/g, '');
        const withoutQuery = clean.split('?')[0].split('#')[0];
        if (!withoutQuery || withoutQuery === 'index.php' || withoutQuery === 'index') return 'index';
        return withoutQuery.replace(/\.php$/, '');
    };

    const markActiveNav = () => {
        const current = normalizePath(window.location.pathname.split('/').pop() || 'index');
        const navLinks = qsa('.nav-menu a');
        navLinks.forEach((link) => {
            const href = normalizePath(link.getAttribute('href') || '');
            const isActive = href !== '' && href === current;
            link.classList.toggle('active', isActive);
        });
    };

    document.addEventListener('DOMContentLoaded', () => {
        const body = document.body;
        const header = qs('header');
        const menuBtn = qs('#mobile-menu');
        const navMenu = qs('.nav-menu');
        const backToTopWrap = qs('.back-to-top');
        const backToTopBtn = qs('#back-to-top-btn');
        const themeToggleBtn = qs('#theme-toggle-btn');
        const themePaletteBtn = qs('#theme-palette-btn');
        const themePalettePanel = qs('#theme-palette-panel');
        const themePaletteGrid = qs('#theme-palette-grid');
        const themePaletteClose = qs('#theme-palette-close');

        const savedTheme = localStorage.getItem('theme');
        if (savedTheme === 'dark' || savedTheme === 'light') {
            body.classList.remove('light-mode', 'dark-mode');
            body.classList.add(`${savedTheme}-mode`);
        } else if (!body.classList.contains('light-mode') && !body.classList.contains('dark-mode')) {
            body.classList.add('light-mode');
        }

        window.dispatchEvent(new Event('tezz:theme-change'));

        if (themeToggleBtn) {
            themeToggleBtn.addEventListener('click', () => {
                const isLight = body.classList.contains('light-mode');
                body.classList.toggle('light-mode', !isLight);
                body.classList.toggle('dark-mode', isLight);
                localStorage.setItem('theme', isLight ? 'dark' : 'light');
                window.dispatchEvent(new Event('tezz:theme-change'));
            });
        }

        const setPalettePanelState = (open) => {
            if (!themePalettePanel || !themePaletteBtn) return;
            themePalettePanel.classList.toggle('open', open);
            themePalettePanel.setAttribute('aria-hidden', open ? 'false' : 'true');
            themePaletteBtn.classList.toggle('active', open);
            themePaletteBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
        };

        const updatePaletteActive = () => {
            if (!themePaletteGrid || !window.MonetThemeEngine) return;
            const currentGroup = window.MonetThemeEngine.getCurrentGroup?.();
            qsa('.palette-group-btn', themePaletteGrid).forEach((btn) => {
                const active = btn.dataset.paletteId === currentGroup;
                btn.classList.toggle('active', active);
                btn.setAttribute('aria-pressed', active ? 'true' : 'false');
            });
        };

        const renderPaletteGroups = () => {
            if (!themePaletteGrid || !window.MonetThemeEngine) return;
            const groups = window.MonetThemeEngine.getPaletteGroups?.() || [];
            if (!Array.isArray(groups) || groups.length === 0) return;

            themePaletteGrid.innerHTML = groups.map((group) => `
                <button class="palette-group-btn" type="button" data-palette-id="${group.id}" aria-pressed="false">
                    <span class="palette-swatches">
                        <span class="palette-swatch" style="background:${group.swatches[0]};"></span>
                        <span class="palette-swatch" style="background:${group.swatches[1]};"></span>
                        <span class="palette-swatch" style="background:${group.swatches[2]};"></span>
                    </span>
                    <span class="palette-group-label">
                        <strong>${group.name}</strong>
                        <span>${group.description}</span>
                    </span>
                    <i class="fas fa-check"></i>
                </button>
            `).join('');

            qsa('.palette-group-btn', themePaletteGrid).forEach((btn) => {
                btn.addEventListener('click', () => {
                    const paletteId = btn.dataset.paletteId || '';
                    if (window.MonetThemeEngine?.applyGroup) {
                        window.MonetThemeEngine.applyGroup(paletteId);
                    } else if (window.MonetThemeEngine?.apply) {
                        const group = groups.find((item) => item.id === paletteId);
                        if (group) window.MonetThemeEngine.apply(group.seed);
                    }
                    updatePaletteActive();
                    setPalettePanelState(false);
                });
            });

            updatePaletteActive();
        };

        if (themePaletteBtn && themePalettePanel) {
            themePaletteBtn.addEventListener('click', (event) => {
                event.stopPropagation();
                if (!themePalettePanel.classList.contains('open')) {
                    renderPaletteGroups();
                    setPalettePanelState(true);
                } else {
                    setPalettePanelState(false);
                }
            });
        }

        if (themePaletteClose) {
            themePaletteClose.addEventListener('click', () => setPalettePanelState(false));
        }

        document.addEventListener('click', (event) => {
            if (!themePalettePanel || !themePaletteBtn) return;
            const insidePanel = event.target.closest('#theme-palette-panel');
            const triggerBtn = event.target.closest('#theme-palette-btn');
            if (!insidePanel && !triggerBtn) setPalettePanelState(false);
        });

        document.addEventListener('keydown', (event) => {
            if (event.key === 'Escape') setPalettePanelState(false);
        });

        window.addEventListener('tezz:palette-change', updatePaletteActive);

        if (window.MonetThemeEngine) {
            renderPaletteGroups();
        }

        if (menuBtn && navMenu) {
            const bars = qsa('.bar', menuBtn);

            const setMenuState = (open) => {
                menuBtn.classList.toggle('active', open);
                navMenu.classList.toggle('active', open);
                menuBtn.setAttribute('aria-expanded', open ? 'true' : 'false');

                if (bars.length === 3) {
                    bars[0].style.transform = open ? 'rotate(45deg) translate(4px, 4px)' : 'none';
                    bars[1].style.opacity = open ? '0' : '1';
                    bars[2].style.transform = open ? 'rotate(-45deg) translate(4px, -4px)' : 'none';
                }
            };

            menuBtn.addEventListener('click', () => {
                const open = !navMenu.classList.contains('active');
                setMenuState(open);
            });

            document.addEventListener('click', (event) => {
                const clickedInsideMenu = event.target.closest('.nav-menu') || event.target.closest('.menu-toggle');
                if (!clickedInsideMenu && navMenu.classList.contains('active')) {
                    setMenuState(false);
                }
            });

            qsa('.nav-menu a').forEach((link) => {
                link.addEventListener('click', () => {
                    if (navMenu.classList.contains('active')) {
                        setMenuState(false);
                    }
                });
            });

            window.addEventListener('resize', () => {
                if (window.innerWidth > 980 && navMenu.classList.contains('active')) {
                    setMenuState(false);
                }
            });
        }

        markActiveNav();

        const onScroll = () => {
            const y = window.scrollY || 0;

            if (header) {
                header.classList.toggle('scrolled', y > 36);
            }

            if (backToTopWrap) {
                backToTopWrap.classList.toggle('show', y > 280);
            }
        };

        window.addEventListener('scroll', onScroll, { passive: true });
        onScroll();

        if (backToTopBtn) {
            backToTopBtn.addEventListener('click', () => {
                window.scrollTo({ top: 0, behavior: 'smooth' });
            });
        }

        qsa('a[href^="#"]').forEach((anchor) => {
            anchor.addEventListener('click', (e) => {
                const href = anchor.getAttribute('href') || '';
                if (href.length <= 1) return;
                const target = qs(href);
                if (!target) return;

                e.preventDefault();
                const offset = header ? header.offsetHeight + 12 : 0;
                const top = target.getBoundingClientRect().top + window.scrollY - offset;
                window.scrollTo({ top, behavior: 'smooth' });
            });
        });

        const revealItems = qsa('.reveal');
        if (revealItems.length > 0) {
            const observer = new IntersectionObserver((entries, obs) => {
                entries.forEach((entry) => {
                    if (!entry.isIntersecting) return;
                    entry.target.classList.add('visible');
                    obs.unobserve(entry.target);
                });
            }, { threshold: 0.15, rootMargin: '0px 0px -40px 0px' });

            revealItems.forEach((item) => observer.observe(item));
        }

        const testimonialItems = qsa('.testimonial-item');
        const dots = qsa('.dot');
        const prevBtn = qs('.prev-btn');
        const nextBtn = qs('.next-btn');

        if (testimonialItems.length > 0) {
            let currentIndex = 0;
            let intervalId = null;

            const render = () => {
                testimonialItems.forEach((item, i) => item.classList.toggle('active', i === currentIndex));
                dots.forEach((dot, i) => dot.classList.toggle('active', i === currentIndex));
            };

            const next = () => {
                currentIndex = (currentIndex + 1) % testimonialItems.length;
                render();
            };

            const prev = () => {
                currentIndex = (currentIndex - 1 + testimonialItems.length) % testimonialItems.length;
                render();
            };

            if (nextBtn) nextBtn.addEventListener('click', next);
            if (prevBtn) prevBtn.addEventListener('click', prev);
            dots.forEach((dot, i) => dot.addEventListener('click', () => {
                currentIndex = i;
                render();
            }));

            const slider = qs('.testimonial-slider');
            const startAuto = () => {
                intervalId = window.setInterval(next, 5000);
            };
            const stopAuto = () => {
                if (intervalId) window.clearInterval(intervalId);
                intervalId = null;
            };

            if (slider) {
                slider.addEventListener('mouseenter', stopAuto);
                slider.addEventListener('mouseleave', startAuto);
            }

            render();
            startAuto();
        }
    });

    window.addEventListener('load', () => {
        const preloader = document.querySelector('.preloader');
        if (!preloader) return;
        preloader.classList.add('fade-out');
        window.setTimeout(() => {
            preloader.style.display = 'none';
        }, 420);
    });
})();
