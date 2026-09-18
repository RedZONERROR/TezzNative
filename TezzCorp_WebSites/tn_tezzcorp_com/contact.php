<?php
declare(strict_types=1);
$page_title = 'Contact TezzCorp — Get a Quote or Book a Consultation';
$page_desc  = 'Contact TezzCorp Pvt Ltd for software development, CRM, API platforms, or consultations. Bihar-based, India-wide delivery.';
$active_nav = 'contact';
include 'includes/header.php';
?>
<main id="main-content">

  <!-- Hero -->
  <section style="padding:4.5rem 0 3.5rem;border-bottom:1px solid var(--border)">
    <div class="container">
      <div class="eyebrow">Reach Us</div>
      <h1 id="contact-page-heading">Let's build something great</h1>
      <p style="font-size:1.125rem;margin-top:.75rem;max-width:540px">
        Have a project in mind? Need a consultation? Or just want to learn more about our services?
        We're ready to talk.
      </p>
    </div>
  </section>

  <!-- Contact Form + Info -->
  <section class="section" aria-labelledby="contact-page-heading">
    <div class="container">
      <div style="display:grid;grid-template-columns:1.4fr 1fr;gap:4rem;align-items:start">

        <!-- Form -->
        <div>
          <h2 style="font-size:1.5rem;margin-bottom:.5rem">Send us a message</h2>
          <p style="margin-bottom:2rem">We respond to all inquiries within 1 business day.</p>

          <form id="contact-form" novalidate style="display:flex;flex-direction:column;gap:1.25rem">
            <div style="display:grid;grid-template-columns:1fr 1fr;gap:1.25rem">
              <div>
                <label for="cf-name" style="display:block;font-size:.875rem;font-weight:600;margin-bottom:.375rem">
                  Full Name <span style="color:var(--red)">*</span>
                </label>
                <input type="text" id="cf-name" name="name" required placeholder="Rajesh Kumar" autocomplete="name"
                  style="width:100%;padding:.6875rem 1rem;background:var(--bg2);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none;transition:border-color .2s"
                  onfocus="this.style.borderColor='var(--blue)'" onblur="this.style.borderColor='var(--border2)'">
              </div>
              <div>
                <label for="cf-email" style="display:block;font-size:.875rem;font-weight:600;margin-bottom:.375rem">
                  Email Address <span style="color:var(--red)">*</span>
                </label>
                <input type="email" id="cf-email" name="email" required placeholder="you@example.com" autocomplete="email"
                  style="width:100%;padding:.6875rem 1rem;background:var(--bg2);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none;transition:border-color .2s"
                  onfocus="this.style.borderColor='var(--blue)'" onblur="this.style.borderColor='var(--border2)'">
              </div>
            </div>

            <div style="display:grid;grid-template-columns:1fr 1fr;gap:1.25rem">
              <div>
                <label for="cf-phone" style="display:block;font-size:.875rem;font-weight:600;margin-bottom:.375rem">
                  Phone Number
                </label>
                <input type="tel" id="cf-phone" name="phone" placeholder="+91 9876543210" autocomplete="tel"
                  style="width:100%;padding:.6875rem 1rem;background:var(--bg2);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none;transition:border-color .2s"
                  onfocus="this.style.borderColor='var(--blue)'" onblur="this.style.borderColor='var(--border2)'">
              </div>
              <div>
                <label for="cf-service" style="display:block;font-size:.875rem;font-weight:600;margin-bottom:.375rem">
                  Service Needed
                </label>
                <select id="cf-service" name="service"
                  style="width:100%;padding:.6875rem 1rem;background:var(--bg2);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none;transition:border-color .2s;cursor:pointer"
                  onfocus="this.style.borderColor='var(--blue)'" onblur="this.style.borderColor='var(--border2)'">
                  <option value="">Select a service...</option>
                  <option value="crm">Sales CRM / Client Portal</option>
                  <option value="website">Website Engineering</option>
                  <option value="api">API Gateway / Platform</option>
                  <option value="erp">School ERP System</option>
                  <option value="automation">Workflow Automation</option>
                  <option value="tezznative">TezzNative / TezzLLM</option>
                  <option value="other">Other / General Inquiry</option>
                </select>
              </div>
            </div>

            <div>
              <label for="cf-subject" style="display:block;font-size:.875rem;font-weight:600;margin-bottom:.375rem">
                Subject <span style="color:var(--red)">*</span>
              </label>
              <input type="text" id="cf-subject" name="subject" required placeholder="Brief description of your project"
                style="width:100%;padding:.6875rem 1rem;background:var(--bg2);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none;transition:border-color .2s"
                onfocus="this.style.borderColor='var(--blue)'" onblur="this.style.borderColor='var(--border2)'">
            </div>

            <div>
              <label for="cf-message" style="display:block;font-size:.875rem;font-weight:600;margin-bottom:.375rem">
                Message <span style="color:var(--red)">*</span>
              </label>
              <textarea id="cf-message" name="message" required rows="5" placeholder="Describe your project, requirements, or questions..."
                style="width:100%;padding:.6875rem 1rem;background:var(--bg2);border:1px solid var(--border2);border-radius:var(--radius);color:var(--text);font-size:.9375rem;font-family:var(--font);outline:none;resize:vertical;min-height:140px;transition:border-color .2s"
                onfocus="this.style.borderColor='var(--blue)'" onblur="this.style.borderColor='var(--border2)'"></textarea>
            </div>

            <div id="form-error" role="alert" style="display:none;background:rgba(239,68,68,.1);border:1px solid rgba(239,68,68,.25);border-radius:var(--radius);padding:.875rem 1rem;font-size:.875rem;color:var(--red)"></div>
            <div id="form-success" role="status" style="display:none;background:rgba(16,185,129,.1);border:1px solid rgba(16,185,129,.25);border-radius:var(--radius);padding:.875rem 1rem;font-size:.875rem;color:var(--green)"></div>

            <button class="btn btn-primary btn-lg" type="submit" id="cf-submit" style="align-self:flex-start">
              <i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;font-size:1.1rem;vertical-align:middle">send</i>
              Send Message
            </button>
          </form>
        </div>

        <!-- Contact Info -->
        <div>
          <h2 style="font-size:1.5rem;margin-bottom:.5rem">Contact information</h2>
          <p style="margin-bottom:2rem">Reach us directly through any of the channels below.</p>

          <div style="display:flex;flex-direction:column;gap:1.25rem">
            <?php $contacts = [
              ['icon'=>'place','c'=>'var(--blue)','b'=>'rgba(59,130,246,.1)','label'=>'Office Address','val'=>'Jhiliya, Bettiah, Bihar – 845438, India'],
              ['icon'=>'phone','c'=>'var(--green)','b'=>'rgba(16,185,129,.1)','label'=>'Phone / WhatsApp','val'=>'+91 9608083342','link'=>'tel:+919608083342'],
              ['icon'=>'email','c'=>'var(--indigo)','b'=>'rgba(99,102,241,.1)','label'=>'Email','val'=>'info@tezzcorp.com','link'=>'mailto:info@tezzcorp.com'],
              ['icon'=>'language','c'=>'var(--cyan)','b'=>'rgba(6,182,212,.1)','label'=>'Website','val'=>'tezzcorp.com','link'=>'https://tezzcorp.com'],
            ]; foreach ($contacts as $c): ?>
            <div style="display:flex;align-items:flex-start;gap:1rem;background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius-lg);padding:1.25rem">
              <div style="width:40px;height:40px;min-width:40px;border-radius:10px;background:<?=$c['b']?>;color:<?=$c['c']?>;display:flex;align-items:center;justify-content:center;font-size:1rem">
                <i class="material-icons" aria-hidden="true"><?=$c['icon']?></i>
              </div>
              <div>
                <div style="font-size:.75rem;font-weight:700;text-transform:uppercase;letter-spacing:.08em;color:var(--dim);margin-bottom:.25rem"><?=$c['label']?></div>
                <?php if (!empty($c['link'])): ?>
                <a href="<?=$c['link']?>" <?=str_starts_with($c['link'],'http')?'target="_blank" rel="noopener"':''?> style="color:var(--text);font-weight:500"><?=$c['val']?></a>
                <?php else: ?>
                <div style="color:var(--text);font-weight:500"><?=$c['val']?></div>
                <?php endif; ?>
              </div>
            </div>
            <?php endforeach; ?>
          </div>

          <div style="background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius-lg);padding:1.5rem;margin-top:1.5rem">
            <h3 style="font-size:1rem;margin-bottom:.875rem">Business Hours</h3>
            <?php $hours = [
              ['Mon – Fri','9:00 AM – 7:00 PM IST'],
              ['Saturday','10:00 AM – 4:00 PM IST'],
              ['Sunday','Emergency only'],
            ]; foreach ($hours as $h): ?>
            <div style="display:flex;justify-content:space-between;padding:.4375rem 0;border-bottom:1px solid var(--border);font-size:.875rem">
              <span style="color:var(--muted)"><?=$h[0]?></span>
              <span style="color:var(--text);font-weight:600"><?=$h[1]?></span>
            </div>
            <?php endforeach; ?>
          </div>

          <div style="background:linear-gradient(135deg,rgba(59,130,246,.08),rgba(99,102,241,.08));border:1px solid rgba(99,102,241,.2);border-radius:var(--radius-lg);padding:1.5rem;margin-top:1.25rem">
            <h3 style="font-size:1rem;margin-bottom:.5rem">⚡ Typical response time</h3>
            <p style="font-size:.875rem;color:var(--muted)">We respond to most inquiries within 4–8 hours during business hours. For urgent projects, call us directly.</p>
          </div>
        </div>
      </div>
    </div>
  </section>
</main>

<script>
document.getElementById('contact-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var err = document.getElementById('form-error');
  var suc = document.getElementById('form-success');
  var btn = document.getElementById('cf-submit');
  err.style.display = 'none';
  suc.style.display = 'none';

  var name = document.getElementById('cf-name').value.trim();
  var email = document.getElementById('cf-email').value.trim();
  var subject = document.getElementById('cf-subject').value.trim();
  var message = document.getElementById('cf-message').value.trim();

  if (!name || !email || !subject || !message) {
    err.textContent = 'Please fill in all required fields (Name, Email, Subject, Message).';
    err.style.display = 'block';
    return;
  }
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
    err.textContent = 'Please enter a valid email address.';
    err.style.display = 'block';
    return;
  }

  btn.innerHTML = '<i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;font-size:1.1rem;vertical-align:middle;animation:spin 1s linear infinite">sync</i> Sending...';
  btn.disabled = true;

  // Simulate form submission (in production, use fetch/AJAX to backend)
  setTimeout(function() {
    suc.textContent = '✓ Message sent! We\'ll get back to you within 1 business day. Thank you, ' + name + '!';
    suc.style.display = 'block';
    btn.innerHTML = '<i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;font-size:1.1rem;vertical-align:middle">check</i> Sent!';
    document.getElementById('contact-form').reset();
    setTimeout(function() {
      btn.innerHTML = '<i class="material-icons" aria-hidden="true" style="margin-right:0.3rem;font-size:1.1rem;vertical-align:middle">send</i> Send Message';
      btn.disabled = false;
    }, 5000);
  }, 1200);
});
</script>

<style>
@media(max-width:768px){
  #main-content .container > div[style*="grid-template-columns:1.4fr"]{
    grid-template-columns:1fr!important;
    gap:2.5rem!important;
  }
  #main-content .container > div > div:first-child > div[style*="grid-template-columns:1fr 1fr"]{
    grid-template-columns:1fr!important;
  }
}
</style>

<?php include 'includes/footer.php'; ?>
