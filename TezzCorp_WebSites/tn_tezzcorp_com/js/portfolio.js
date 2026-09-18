document.addEventListener('DOMContentLoaded', () => {
    const filterButtons = Array.from(document.querySelectorAll('.filter-btn'));
    const portfolioItems = Array.from(document.querySelectorAll('.portfolio-item'));
    const projectModal = document.getElementById('project-modal');
    const closeModalBtn = document.querySelector('.close-modal');

    const parseCaseData = (item) => {
        const raw = item?.getAttribute('data-case') || '{}';
        try {
            const parsed = JSON.parse(raw);
            return typeof parsed === 'object' && parsed !== null ? parsed : {};
        } catch (_e) {
            return {};
        }
    };

    const applyFilter = (filterValue) => {
        portfolioItems.forEach((item) => {
            const category = (item.getAttribute('data-category') || '').toLowerCase();
            const shouldShow = filterValue === 'all' || category === filterValue;

            if (shouldShow) {
                item.style.display = 'block';
                requestAnimationFrame(() => {
                    item.style.opacity = '1';
                    item.style.transform = 'scale(1)';
                });
            } else {
                item.style.opacity = '0';
                item.style.transform = 'scale(0.96)';
                window.setTimeout(() => {
                    item.style.display = 'none';
                }, 220);
            }
        });
    };

    filterButtons.forEach((button) => {
        button.addEventListener('click', () => {
            filterButtons.forEach((btn) => btn.classList.remove('active'));
            button.classList.add('active');
            applyFilter((button.getAttribute('data-filter') || 'all').toLowerCase());
        });
    });

    const fillModal = (payload) => {
        const title = payload.title || 'Case Study';
        const client = payload.client || '-';
        const category = payload.category || payload.industry || '-';
        const description = payload.description || '-';
        const challenge = payload.challenge || '-';
        const solution = payload.solution || '-';
        const impactLabel = payload.impact_label || 'Impact';
        const impactValue = payload.impact_value || '-';
        const image = payload.image || 'img/web.png';
        const ctaLabel = payload.cta_label || 'Contact Team';
        const ctaUrl = payload.cta_url || 'contact';
        const technologies = Array.isArray(payload.technologies) ? payload.technologies : [];

        const setText = (id, value) => {
            const node = document.getElementById(id);
            if (node) node.textContent = value;
        };

        setText('modal-project-title', title);
        setText('modal-client', client);
        setText('modal-category', category);
        setText('modal-description', description);
        setText('modal-challenge', challenge);
        setText('modal-solution', solution);
        setText('modal-impact-label', `${impactLabel}:`);
        setText('modal-impact-value', impactValue);

        const imageNode = document.getElementById('modal-main-image');
        if (imageNode) imageNode.src = image;

        const techRoot = document.getElementById('modal-technologies');
        if (techRoot) {
            techRoot.innerHTML = '';
            technologies.forEach((tech) => {
                const chip = document.createElement('span');
                chip.className = 'tech-tag';
                chip.textContent = String(tech || '').trim();
                if (chip.textContent !== '') techRoot.appendChild(chip);
            });
            if (techRoot.children.length === 0) {
                const chip = document.createElement('span');
                chip.className = 'tech-tag';
                chip.textContent = 'Custom Stack';
                techRoot.appendChild(chip);
            }
        }

        const ctaNode = document.getElementById('modal-live-link');
        if (ctaNode) {
            ctaNode.href = ctaUrl;
            ctaNode.innerHTML = `<span>${ctaLabel}</span><i class="fas fa-arrow-right"></i>`;
        }
    };

    document.querySelectorAll('.btn-view-project').forEach((button) => {
        button.addEventListener('click', (event) => {
            event.preventDefault();
            const item = button.closest('.portfolio-item');
            const payload = parseCaseData(item);
            fillModal(payload);

            if (projectModal) {
                projectModal.style.display = 'block';
                document.body.style.overflow = 'hidden';
            }
        });
    });

    const closeModal = () => {
        if (!projectModal) return;
        projectModal.style.display = 'none';
        document.body.style.overflow = 'auto';
    };

    closeModalBtn?.addEventListener('click', closeModal);

    window.addEventListener('click', (event) => {
        if (event.target === projectModal) closeModal();
    });

    window.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') closeModal();
    });

    applyFilter('all');
});
