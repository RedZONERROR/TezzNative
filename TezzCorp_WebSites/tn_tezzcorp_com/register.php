<?php require_once 'config/config.php'; ?>
<?php require_once 'config/config.php'; ?>

<!DOCTYPE html>
<html lang="en-IN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Register - TezzCorp</title>
    <meta name="description" content="Create an account with TezzCorp to access our services and manage your projects.">
    <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
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
    
    <!-- Register Section -->
    <section class="auth-section">
        <div class="auth-shape"></div>
        <div class="auth-shape-2"></div>
        <div class="container">
            <div class="auth-container">
                <div class="auth-image">
                    <img src="https://images.unsplash.com/photo-1522071820081-009f0129c71c?ixlib=rb-1.2.1&auto=format&fit=crop&w=600&q=80" alt="Register Image">
                    <div class="image-overlay">
                        <div class="overlay-content">
                            <h2>Join Our Community</h2>
                            <p>Create an account to access our services and manage your projects.</p>
                        </div>
                    </div>
                </div>
                <div class="auth-form-container">
                    <div class="form-header">
                        <h2>Register</h2>
                        <p>Create your account to get started</p>
                    </div>
                    <form class="auth-form" id="register-form">
                        <input type="hidden" name="csrf_token" value="<?php echo generateCsrfToken(); ?>">

                        <div class="form-group">
                            <label for="name"><i class="material-icons" aria-hidden="true" style="font-size:1.25rem">person</i></label>
                            <input type="text" id="name" placeholder="Full Name" required>
                        </div>
                        <div class="form-group">
                            <label for="email"><i class="material-icons" aria-hidden="true" style="font-size:1.25rem">email</i></label>
                            <input type="email" id="email" placeholder="Email Address" required>
                        </div>
                        <div class="form-group">
                            <label for="phone"><i class="material-icons" aria-hidden="true" style="font-size:1.25rem">phone</i></label>
                            <input type="tel" id="phone" placeholder="Phone Number" required>
                        </div>
                        <div class="form-group">
                            <label for="password"><i class="material-icons" aria-hidden="true" style="font-size:1.25rem">lock</i></label>
                            <input type="password" id="password" placeholder="Password" required>
                            <button type="button" class="toggle-password" id="toggle-password">
                                <i class="material-icons" aria-hidden="true" style="font-size:1.2rem">visibility</i>
                            </button>
                        </div>
                        <div class="form-group">
                            <label for="confirm-password"><i class="material-icons" aria-hidden="true" style="font-size:1.25rem">lock</i></label>
                            <input type="password" id="confirm-password" placeholder="Confirm Password" required>
                            <button type="button" class="toggle-password" id="toggle-confirm-password">
                                <i class="material-icons" aria-hidden="true" style="font-size:1.2rem">visibility</i>
                            </button>
                        </div>
                        <div class="form-options">
                            <div class="terms-checkbox">
                                <input type="checkbox" id="terms" required>
                                <label for="terms">I agree to the <a href="#">Terms & Conditions</a></label>
                            </div>
                        </div>
                        <button type="submit" class="btn btn-primary login-button">Register</button>
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
                            <p>Already have an account? <a href="login.php">Login</a></p>
                        </div>
                    </form>
                </div>
            </div>
        </div>
    </section>

    <?php include 'footer.php'; ?>

    <!-- Scripts -->
    <script src="js/main.js?v=<?php echo time(); ?>"></script>
    <script src="js/auth.js?v=<?php echo time(); ?>"></script>
    
    <script>
      const CRM_REDIRECT_FALLBACK = <?php echo json_encode(CRM_URL . '/index.php'); ?>;

      document.addEventListener('DOMContentLoaded', function () {
        const form = document.getElementById('register-form');
        const btn = form ? form.querySelector('.login-button') : null;
    
        // Simple toast (no external libs)
        function toast(message, type = 'error') {
          const el = document.createElement('div');
          el.style.position = 'fixed';
          el.style.top = '20px';
          el.style.right = '20px';
          el.style.zIndex = 99999;
          el.style.maxWidth = '360px';
          el.style.padding = '12px 14px';
          el.style.borderRadius = '10px';
          el.style.boxShadow = '0 10px 25px rgba(0,0,0,.15)';
          el.style.fontFamily = 'Manrope, sans-serif';
              body: JSON.stringify({ name, email, phone, password })
            });
    
            const data = await res.json().catch(() => ({}));
    
            if (data.success) {
              toast('Registration successful! Redirecting...', 'success');
              setTimeout(() => {
                window.location.href = data.redirect || CRM_REDIRECT_FALLBACK;
              }, 600);
            } else {
              toast(data.message || 'Registration failed.', 'error');
              if (btn) { btn.disabled = false; btn.innerHTML = original; }
            }
          } catch (err) {
            toast('Network error. Please try again.', 'error');
            if (btn) { btn.disabled = false; btn.innerHTML = original; }
          }
        });
      });
    </script>
</body>
</html>
