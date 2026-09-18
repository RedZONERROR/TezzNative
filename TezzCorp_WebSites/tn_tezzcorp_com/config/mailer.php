<?php
declare(strict_types=1);

/**
 * TezzCRM Unified Mailer Engine (Stage 5)
 * Uses PHPMailer with database-driven SMTP configuration.
 */

require_once __DIR__ . '/../vendor/autoload.php';
require_once __DIR__ . '/config.php';

use PHPMailer\PHPMailer\PHPMailer;
use PHPMailer\PHPMailer\Exception;

/**
 * Sends a premium HTML email using TezzCorp SMTP settings.
 *
 * @param string $to
 * @param string $subject
 * @param string $htmlBody
 * @param string $plainText (optional)
 * @return array ['success' => bool, 'message' => string]
 */
function sendCrmEmail(string $to, string $subject, string $htmlBody, string $plainText = ''): array
{
    try {
        $pdo = getDBConnection();
        $orgId = 'be4398857eca2f2542a46ae7eae537a5'; // Correct Hostinger Org ID (Tezz Corp)

        // 1. Fetch SMTP settings from DB
        $stmt = $pdo->prepare("SELECT * FROM crm_mail_settings WHERE organization_id = ? LIMIT 1");
        $stmt->execute([$orgId]);
        $cfg = $stmt->fetch(PDO::FETCH_ASSOC);

        if (!$cfg || empty($cfg['smtp_host'])) {
            // Fallback to constants if DB settings missing
            $cfg = [
                'smtp_host'  => 'smtp.hostinger.com',
                'smtp_port'  => 587,
                'smtp_user'  => 'info@bthdevelopers.com',
                'smtp_pass'  => 'Dev.Bth@845438',
                'from_email' => 'info@bthdevelopers.com',
                'from_name'  => 'Bth Developers'
            ];
        }

        $mail = new PHPMailer(true);

        // Server settings
        $mail->isSMTP();
        $mail->Host       = $cfg['smtp_host'];
        $mail->SMTPAuth   = true;
        $mail->Username   = $cfg['smtp_user'];
        $mail->Password   = $cfg['smtp_pass'];
        $mail->SMTPSecure = PHPMailer::ENCRYPTION_STARTTLS;
        $mail->Port       = (int)$cfg['smtp_port'];
        $mail->CharSet    = 'UTF-8';

        // Recipients
        $mail->setFrom($cfg['from_email'], $cfg['from_name']);
        $mail->addAddress($to);

        // Content
        $mail->isHTML(true);
        $mail->Subject = $subject;
        
        // Wrap body in premium TezzCorp template
        $fullHtml = "
        <div style='background-color:#060b15; padding:40px 20px; font-family:sans-serif; color:#f2f6ff;'>
            <div style='max-width:600px; margin:0 auto; background:rgba(10,18,35,0.84); border:1px solid rgba(148,163,184,0.28); border-radius:16px; padding:30px;'>
                <div style='text-align:center; margin-bottom:30px;'>
                    <img src='https://www.vspublicschool.co.in/logo.svg' alt='TezzCorp' style='height:50px;'>
                    <h2 style='color:#60a5fa; margin-top:10px;'>TezzCorp CRM</h2>
                </div>
                <div style='line-height:1.6; font-size:15px;'>
                    {$htmlBody}
                </div>
                <div style='margin-top:40px; padding-top:20px; border-top:1px solid rgba(148,163,184,0.2); font-size:12px; color:#c0cee8; text-align:center;'>
                    &copy; 2026 TezzCorp Platform &bull; Intelligent ERP Solutions
                </div>
            </div>
        </div>";

        $mail->Body    = $fullHtml;
        $mail->AltBody = $plainText ?: strip_tags($htmlBody);

        $mail->send();

        // Log delivery
        $pdo->prepare("INSERT INTO crm_notification_deliveries (id, organization_id, type, recipient_email, subject, status) VALUES (?, ?, ?, ?, ?, ?)")
            ->execute([bin2hex(random_bytes(16)), $orgId, 'email', $to, $subject, 'sent']);

        return ['success' => true, 'message' => 'Email sent successfully'];

    } catch (Exception $e) {
        return ['success' => false, 'message' => "Mailer Error: {$mail->ErrorInfo}"];
    } catch (Throwable $e) {
        return ['success' => false, 'message' => "System Error: " . $e->getMessage()];
    }
}
