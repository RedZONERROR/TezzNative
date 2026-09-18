document.addEventListener('DOMContentLoaded', function () {
    const contactForm = document.getElementById('contact-form');
    const faqItems = document.querySelectorAll('.faq-item');
    const submitBtn = document.getElementById('contact-submit-btn');

    function showToast(message, type) {
        const toast = document.createElement('div');
        toast.style.position = 'fixed';
        toast.style.right = '20px';
        toast.style.top = '20px';
        toast.style.zIndex = '99999';
        toast.style.padding = '12px 14px';
        toast.style.borderRadius = '10px';
        toast.style.boxShadow = '0 8px 20px rgba(0,0,0,.18)';
        toast.style.fontSize = '14px';
        toast.style.maxWidth = '360px';
        toast.style.background = type === 'success' ? '#ecfdf5' : '#fef2f2';
        toast.style.border = type === 'success' ? '1px solid #86efac' : '1px solid #fecaca';
        toast.style.color = type === 'success' ? '#065f46' : '#991b1b';
        toast.textContent = message;
        document.body.appendChild(toast);

        setTimeout(() => {
            toast.style.transition = 'all .25s ease';
            toast.style.opacity = '0';
            toast.style.transform = 'translateY(-6px)';
            setTimeout(() => toast.remove(), 260);
        }, 2600);
    }

    function setSubmitState(state) {
        if (!submitBtn) return;

        submitBtn.classList.remove('is-sending', 'is-sent');
        if (state === 'sending') {
            submitBtn.classList.add('is-sending');
            submitBtn.disabled = true;
            return;
        }
        if (state === 'sent') {
            submitBtn.classList.add('is-sent');
            submitBtn.disabled = true;
            return;
        }
        submitBtn.disabled = false;
    }

    if (contactForm) {
        contactForm.addEventListener('submit', async function (e) {
            e.preventDefault();

            const name = document.getElementById('name').value.trim();
            const email = document.getElementById('email').value.trim();
            const phone = document.getElementById('phone').value.trim();
            const subject = document.getElementById('subject').value.trim();
            const message = document.getElementById('message').value.trim();
            const terms = document.getElementById('terms').checked;

            if (!terms) {
                showToast('Please accept Terms & Conditions.', 'error');
                return;
            }
            if (!name || !email || !message) {
                showToast('Name, email and message are required.', 'error');
                return;
            }

            setSubmitState('sending');

            try {
                const res = await fetch('api/contact.php', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ name, email, phone, subject, message })
                });
                const data = await res.json().catch(() => ({}));

                if (!data || !data.ok) {
                    setSubmitState('idle');
                    showToast('Unable to send your message right now.', 'error');
                    return;
                }

                showToast('Message received. Our team will contact you soon.', 'success');
                contactForm.reset();
                setSubmitState('sent');
                window.setTimeout(() => setSubmitState('idle'), 1200);
            } catch (err) {
                setSubmitState('idle');
                showToast('Network error. Please try again.', 'error');
            }
        });
    }

    faqItems.forEach((item) => {
        const question = item.querySelector('.faq-question');
        if (!question) return;
        question.addEventListener('click', function () {
            faqItems.forEach((faqItem) => {
                if (faqItem !== item) faqItem.classList.remove('active');
            });
            item.classList.toggle('active');
        });
    });
});
