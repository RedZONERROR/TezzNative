<footer class="site-footer">
    <div class="container">
        <div class="footer-top">
            <div class="footer-brand">
                <div class="brand-lockup">
                    <img src="img/tezz-corp-logo.png" alt="TezzCorp Logo">
                    <h2>Tezz<span class="dynamic-gradient-text">Corp</span></h2>
                </div>
                <p>TezzCorp builds and operates secure software systems, revenue-focused websites, and scalable API platforms for growth-stage and enterprise teams.</p>
                <div class="pill-list">
                    <span class="pill">CRM Operations</span>
                    <span class="pill">Website Engineering</span>
                    <span class="pill">API Products</span>
                </div>
            </div>

            <div class="footer-columns">
                <div class="footer-column">
                    <h3>Company</h3>
                    <ul>
                        <li><a href="about">About</a></li>
                        <li><a href="services">Services</a></li>
                        <li><a href="portfolio">Case Studies</a></li>
                        <li><a href="contact">Contact</a></li>
                    </ul>
                </div>

                <div class="footer-column">
                    <h3>Platforms</h3>
                    <ul>
                        <li><a href="software">Product Suite</a></li>
                        <li><a href="api-docs">API Documentation</a></li>
                        <li><a href="login">CRM Console</a></li>
                        <li><a href="privacy">Privacy Policy</a></li>
                        <li><a href="terms">Terms of Service</a></li>
                    </ul>
                </div>

                <div class="footer-column">
                    <h3>Contact</h3>
                    <ul class="contact-list">
                        <li><i class="material-icons" aria-hidden="true">location_on</i> Jhiliya, Bettiah, Bihar - 845438</li>
                        <li><i class="material-icons" aria-hidden="true">phone</i> <a href="tel:+919608083342">+91 9608083342</a></li>
                        <li><i class="material-icons" aria-hidden="true">email</i> <a href="mailto:info@tezzcorp.com">info@tezzcorp.com</a></li>
                        <li><i class="material-icons" aria-hidden="true">language</i> <a href="https://www.tezzcorp.com" target="_blank" rel="noopener">www.tezzcorp.com</a></li>
                    </ul>
                </div>
            </div>
        </div>

        <div class="footer-bottom">
            <div class="footer-bottom-info" style="line-height: 1.6;">
                <p>&copy; <?php echo date('Y'); ?> <a href="https://tezzcorp.com" style="color: var(--text-color); font-weight: 600;">TezzCorp Pvt Ltd</a>. All rights reserved. | CIN: U62011BR2026PTC083413</p>
                <p style="font-size: 0.8rem; opacity: 0.7; margin-top: 4px;">
                    TezzCorp platforms and systems are created, designed, and directed by <strong>Rohit Pathak</strong>, Creator & Director.
                </p>
            </div>
            <div class="footer-inline">
                <a href="privacy">Privacy</a>
                <a href="terms">Terms</a>
                <div class="payment-methods">
                    <span>Payments</span>
                    <img src="upi-logo.png" alt="UPI">
                    <img src="paypal.webp" alt="PayPal">
                </div>
            </div>
        </div>
    </div>
</footer>

<div class="theme-toggle">
    <button id="theme-palette-btn" aria-label="Open color palette" aria-expanded="false" aria-controls="theme-palette-panel" title="Choose color group">
        <i class="material-icons">palette</i>
    </button>
    <button id="theme-toggle-btn" aria-label="Toggle theme">
        <i class="material-icons dark-icon">dark_mode</i>
        <i class="material-icons light-icon">light_mode</i>
    </button>
</div>

<div class="back-to-top">
    <button id="back-to-top-btn" aria-label="Back to top">
        <i class="material-icons">arrow_upward</i>
    </button>
</div>

<aside class="theme-palette-panel" id="theme-palette-panel" aria-hidden="true">
    <div class="palette-panel-head">
        <h3>Color Groups</h3>
        <button id="theme-palette-close" type="button" aria-label="Close color palette">
            <i class="material-icons">close</i>
        </button>
    </div>
    <p>Pick a corporate color group. The whole website updates automatically.</p>
    <div class="palette-grid" id="theme-palette-grid"></div>
</aside>

<script src="js/monet-engine.js?v=<?php echo time(); ?>"></script>
