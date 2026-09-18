// Wait for DOM to load
document.addEventListener('DOMContentLoaded', function() {
    // Register form
    const registerForm = document.getElementById('register-form');
    if (registerForm) {
        registerForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            // Get form values
            const firstName = document.getElementById('first-name').value;
            const lastName = document.getElementById('last-name').value;
            const email = document.getElementById('email').value;
            const phone = document.getElementById('phone').value;
            const company = document.getElementById('company').value;
            const password = document.getElementById('password').value;
            const confirmPassword = document.getElementById('confirm-password').value;
            const terms = document.getElementById('terms').checked;
            const newsletter = document.getElementById('newsletter').checked;
            
            // Simple validation
            if (!firstName || !lastName || !email || !phone || !password || !confirmPassword) {
                showNotification('Please fill in all required fields', 'error');
                return;
            }
            
            if (password !== confirmPassword) {
                showNotification('Passwords do not match', 'error');
                return;
            }
            
            if (!terms) {
                showNotification('Please agree to the Terms & Conditions', 'error');
                return;
            }
            
            // Simulate registration
            registerUser(firstName, lastName, email, phone, company, password, newsletter);
        });
    }
    
    // Toggle password visibility
    const togglePassword = document.querySelector('.toggle-password');
    if (togglePassword) {
        togglePassword.addEventListener('click', function() {
            const passwordInput = document.getElementById('password');
            const type = passwordInput.getAttribute('type') === 'password' ? 'text' : 'password';
            passwordInput.setAttribute('type', type);
            
            // Toggle icon
            this.innerHTML = type === 'password' ? '<i class="fas fa-eye"></i>' : '<i class="fas fa-eye-slash"></i>';
        });
    }
    
    // Password strength meter
    const passwordInput = document.getElementById('password');
    const strengthMeter = document.querySelector('.strength-meter-fill');
    const strengthText = document.querySelector('.strength-text');
    
    if (passwordInput && strengthMeter && strengthText) {
        passwordInput.addEventListener('input', function() {
            const password = this.value;
            const strength = calculatePasswordStrength(password);
            
            // Update strength meter
            strengthMeter.style.width = strength.score * 25 + '%';
            strengthMeter.setAttribute('data-strength', strength.score);
            
            // Update strength text
            strengthText.textContent = strength.message;
        });
    }
    
    // Calculate password strength
    function calculatePasswordStrength(password) {
        // Initialize score
        let score = 0;
        
        // If password is empty, return 0
        if (password.length === 0) {
            return { score: 0, message: '' };
        }
        
        // Length check
        if (password.length < 6) {
            return { score: 1, message: 'Weak: Too short' };
        } else if (password.length >= 10) {
            score += 2;
        } else {
            score += 1;
        }
        
        // Complexity checks
        const hasLowercase = /[a-z]/.test(password);
        const hasUppercase = /[A-Z]/.test(password);
        const hasNumbers = /\d/.test(password);
        const hasSpecialChars = /[!@#$%^&*()_+\-=\[\]{};':"\\|,.<>\/?]/.test(password);
        
        if (hasLowercase) score += 0.5;
        if (hasUppercase) score += 0.5;
        if (hasNumbers) score += 0.5;
        if (hasSpecialChars) score += 1;
        
        // Calculate final score (max 4)
        score = Math.min(Math.floor(score), 4);
        
        // Return score and message
        const messages = [
            '',
            'Weak',
            'Fair',
            'Good',
            'Strong'
        ];
        
        return { score, message: messages[score] };
    }
    
    // Social register buttons
    const socialButtons = document.querySelectorAll('.social-btn');
    socialButtons.forEach(button => {
        button.addEventListener('click', function() {
            const provider = this.classList.contains('google') ? 'Google' : 'Facebook';
            showNotification(`${provider} registration is not implemented in this demo`, 'info');
        });
    });
    
    // Simulate user registration
    function registerUser(firstName, lastName, email, phone, company, password, newsletter) {
        // Show loading state
        const registerBtn = document.querySelector('.register-btn');
        const originalText = registerBtn.textContent;
        registerBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Creating Account...';
        registerBtn.disabled = true;
        
        // Simulate API call
        setTimeout(() => {
            // Reset button
            registerBtn.innerHTML = originalText;
            registerBtn.disabled = false;
            
            // Show success message
            showNotification('Account created successfully! Redirecting to login...', 'success');
            
            // Redirect to login page (in a real app)
            setTimeout(() => {
                window.location.href = 'login.html';
            }, 2000);
        }, 2000);
    }
    
    // Show notification
    function showNotification(message, type) {
        // Create notification element
        const notification = document.createElement('div');
        notification.className = `notification ${type}`;
        notification.innerHTML = `
            <div class="notification-content">
                <i class="fas ${type === 'success' ? 'fa-check-circle' : type === 'error' ? 'fa-exclamation-circle' : 'fa-info-circle'}"></i>
                <p>${message}</p>
            </div>
            <button class="close-notification"><i class="fas fa-times"></i></button>
        `;
        
        // Add to DOM
        document.body.appendChild(notification);
        
        // Add styles
        notification.style.position = 'fixed';
        notification.style.bottom = '20px';
        notification.style.right = '20px';
        notification.style.backgroundColor = type === 'success' ? '#4CAF50' : type === 'error' ? '#F44336' : '#2196F3';
        notification.style.color = 'white';
        notification.style.padding = '15px';
        notification.style.borderRadius = '10px';
        notification.style.boxShadow = '0 5px 15px rgba(0, 0, 0, 0.2)';
        notification.style.zIndex = '9999';
        notification.style.display = 'flex';
        notification.style.justifyContent = 'space-between';
        notification.style.alignItems = 'center';
        notification.style.minWidth = '300px';
        notification.style.transform = 'translateX(100%)';
        notification.style.opacity = '0';
        notification.style.transition = 'all 0.3s ease';
        
        // Show notification
        setTimeout(() => {
            notification.style.transform = 'translateX(0)';
            notification.style.opacity = '1';
        }, 10);
        
        // Close button
        const closeBtn = notification.querySelector('.close-notification');
        closeBtn.style.background = 'none';
        closeBtn.style.border = 'none';
        closeBtn.style.color = 'white';
        closeBtn.style.cursor = 'pointer';
        
        closeBtn.addEventListener('click', () => {
            notification.style.transform = 'translateX(100%)';
            notification.style.opacity = '0';
            setTimeout(() => {
                notification.remove();
            }, 300);
        });
        
        // Auto close after 5 seconds
        setTimeout(() => {
            notification.style.transform = 'translateX(100%)';
            notification.style.opacity = '0';
            setTimeout(() => {
                notification.remove();
            }, 300);
        }, 5000);
    }
});