<?php require_once 'config/config.php'; ?>

<!DOCTYPE html>
<html lang="en-IN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Login - TezzCorp</title>
    <meta name="description" content="Login to your TezzCorp account to access your dashboard and manage your projects.">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;500;600;700;800&family=Sora:wght@500;600;700;800&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="css/style.css?v=<?php echo time(); ?>">
    <link rel="stylesheet" href="css/auth.css?v=<?php echo time(); ?>">
    <!-- Favicon -->
    <link rel="apple-touch-icon" sizes="180x180" href="favicon/apple-touch-icon.png">
    <link rel="icon" type="image/png" sizes="32x32" href="favicon/favicon-32x32.png">
    <link rel="icon" type="image/png" sizes="16x16" href="favicon/favicon-16x16.png">
    <link rel="manifest" href="favicon/site.webmanifest">
</head>
<body class="light-mode page-auth">
    
    <?php include 'header.php'; ?>

    <!-- Login Section -->
    <section class="auth-section">
        <div class="auth-shape"></div>
        <div class="auth-shape-2"></div>
        <div class="container">
            <div class="auth-container">
                <div class="auth-image">
                    <img src="https://images.unsplash.com/photo-1551434678-e076c223a692?ixlib=rb-1.2.1&auto=format&fit=crop&w=600&q=80" alt="Login Image">
                    <div class="image-overlay">
                        <div class="overlay-content">
                            <h2>Welcome Back!</h2>
                            <p>Login to access your dashboard and manage your projects.</p>
                        </div>
                    </div>
                </div>
                <div class="auth-form-container">
                    <div class="form-header">
                        <h2>Login</h2>
                        <p>Enter your credentials to access your account</p>
                    </div>
                    <form class="auth-form" id="login-form">
                        <input type="hidden" name="csrf_token" value="<?php echo generateCsrfToken(); ?>">

                        <div class="form-group">
                            <label for="email"><i class="material-icons" aria-hidden="true" style="font-size:1.25rem">email</i></label>
                            <input type="email" id="email" placeholder="Email Address" required>
                        </div>
                        <div class="form-group">
                            <label for="password"><i class="material-icons" aria-hidden="true" style="font-size:1.25rem">lock</i></label>
                            <input type="password" id="password" placeholder="Password" required>
                            <button type="button" class="toggle-password" id="toggle-password">
                                <i class="material-icons" aria-hidden="true" style="font-size:1.2rem">visibility</i>
                            </button>
                        </div>
                        <div class="form-options">
                            <div class="remember-me">
                                <input type="checkbox" id="remember">
                                <label for="remember">Remember me</label>
                            </div>
                            <a href="#" class="forgot-password">Forgot Password?</a>
                        </div>
                        <button type="submit" class="btn btn-primary login-button">Login</button>
                        <div class="social-login">
                            <p>Or login with</p>
                            <div class="social-buttons">
                                <button type="button" class="social-btn tezzauth">
                                    <i class="fab"><img src="img/tezz-corp-logo.png" alt="TezzAuth" style="width: 15px; height: 15px;"></i>
                                    TezzAuth
                                </button>
                            </div>
                        </div>
                        <div class="register-link">
                            <p>Don't have an account? <a href="register">Register</a></p>
                        </div>
                    </form>
                </div>
            </div>
        </div>
    </section>

    <?php include 'footer.php'; ?>

    <!-- Scripts -->
    <script src="js/main.js?v=<?php echo time(); ?>"></script>
    <script>
        const CRM_REDIRECT_FALLBACK = <?php echo json_encode(CRM_URL . '/'); ?>;

        // Authentication Pages JavaScript
        document.addEventListener('DOMContentLoaded', function() {
            // Toggle Password Visibility
            const togglePassword = document.getElementById('toggle-password');
            const passwordInput = document.getElementById('password');
            
            if (togglePassword && passwordInput) {
                togglePassword.addEventListener('click', function() {
                    const type = passwordInput.getAttribute('type') === 'password' ? 'text' : 'password';
                    passwordInput.setAttribute('type', type);
                    
                    // Toggle eye icon
                    const eyeIcon = this.querySelector('i');
                    eyeIcon.textContent = type === 'password' ? 'visibility' : 'visibility_off';
                });
            }
        
            // Login Form Submission
            const loginForm = document.getElementById('login-form');
            const loginButton = loginForm ? loginForm.querySelector('.login-button') : null;
        
            if (loginForm && loginButton) {
                loginForm.addEventListener('submit', function(e) {
                    e.preventDefault();
        
                    const email = document.getElementById('email').value.trim();
                    const password = document.getElementById('password').value.trim();
                    const remember = document.getElementById('remember').checked;
        
                    if (!email || !password) {
                        showAlert('Please fill in all fields', 'error');
                        return;
                    }
        
                    // Show Font Awesome loader
                    const originalButtonContent = loginButton.innerHTML;
                    loginButton.disabled = true;
                    loginButton.innerHTML = '<i class="material-icons" aria-hidden="true" style="font-size:1.1rem;vertical-align:middle;margin-right:0.3rem;animation:spin 1s linear infinite">sync</i> Logging in...';
        
                    // Get CSRF token
                    const csrfToken = document.querySelector('input[name="csrf_token"]')?.value || '';
        
                    fetch('api/auth/login.php', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                            'X-CSRF-Token': csrfToken
                        },
                        body: JSON.stringify({ email, password, remember })
                    })
                    .then(response => {
                        if (!response.ok) {
                            throw new Error('Network response was not ok');
                        }
                        return response.json();
                    })
                    .then(data => {
                        if (data.success) {
                            showAlert('Login successful! Redirecting...', 'success');
                            setTimeout(() => {
                                window.location.href = data.redirect || CRM_REDIRECT_FALLBACK;
                            }, 50);
                            // Restore button state
                            loginButton.disabled = false;
                            loginButton.innerHTML = originalButtonContent;
                        } else {
                            showAlert(data.message || 'Invalid credentials', 'error');
                        }
                    })
                    .catch(error => {
                        // Restore button state
                        loginButton.disabled = false;
                        loginButton.innerHTML = originalButtonContent;
        
                        console.error('Error:', error);
                        showAlert('An error occurred. Please try again.', 'error');
                    });
                });
            }
        
            // Helper function to show alerts
            function showAlert(message, type) {
                const alertContainer = document.createElement('div');
                alertContainer.className = `fixed top-4 right-4 px-4 py-3 rounded-lg shadow-lg transition-all duration-300 
                    ${type === 'success' ? 'bg-green-100 text-green-800' : 'bg-red-100 text-red-800'}`;
                alertContainer.innerHTML = `
                    <div class="flex items-center">
                        <i class="material-icons" aria-hidden="true" style="margin-right:0.3rem">${type === 'success' ? 'check_circle' : 'error'}</i>
                        <span>${message}</span>
                    </div>
                `;
                document.body.appendChild(alertContainer);
        
                // Auto-remove after 5 seconds
                setTimeout(() => {
                    alertContainer.style.opacity = '0';
                    setTimeout(() => alertContainer.remove(), 300);
                }, 5000);
            }
        
            // Form Input Animation
            const formInputs = document.querySelectorAll('.form-group input');
            
            formInputs.forEach(input => {
                input.addEventListener('focus', function() {
                    this.parentElement.classList.add('focused');
                });
                
                input.addEventListener('blur', function() {
                    if (!this.value) {
                        this.parentElement.classList.remove('focused');
                    }
                });
                
                // Check if input already has value
                if (input.value) {
                    input.parentElement.classList.add('focused');
                }
            });
        });
    </script>
</body>
</html>
