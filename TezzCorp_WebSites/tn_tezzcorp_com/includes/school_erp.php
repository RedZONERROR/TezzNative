<?php
declare(strict_types=1);

require_once __DIR__ . '/../config/config.php';

function schoolErpPrimaryPayUrl(): string {
    $url = trim((string)env('PAY_TEZZCORP_URL', 'https://pay.tezzcorp.in/'));
    if ($url === '') {
        $url = 'https://pay.tezzcorp.in/';
    }
    return rtrim($url, '/') . '/';
}

function schoolErpNormalizeGateway(?string $gateway): string {
    $value = strtolower(trim((string)$gateway));
    return in_array($value, ['pay_tezzcorp', 'stripe', 'razorpay'], true)
        ? $value
        : 'pay_tezzcorp';
}

function schoolErpNormalizeBillingCycle(?string $cycle): string {
    $value = strtolower(trim((string)$cycle));
    return in_array($value, ['monthly', 'quarterly', 'annually'], true)
        ? $value
        : 'monthly';
}

function schoolErpParseDate(string $value, ?string $fallback = null): string {
    $text = trim($value);
    if ($text === '' && $fallback !== null) {
        $text = trim($fallback);
    }
    if ($text === '') {
        return gmdate('Y-m-d');
    }

    $dt = DateTimeImmutable::createFromFormat('Y-m-d', $text, new DateTimeZone('UTC'));
    if ($dt instanceof DateTimeImmutable) {
        return $dt->format('Y-m-d');
    }

    $ts = strtotime($text);
    if ($ts === false) {
        return gmdate('Y-m-d');
    }
    return gmdate('Y-m-d', $ts);
}

function schoolErpPeriodEnd(string $periodStartDate, string $billingCycle): string {
    $start = new DateTimeImmutable(schoolErpParseDate($periodStartDate) . ' 00:00:00', new DateTimeZone('UTC'));
    $cycle = schoolErpNormalizeBillingCycle($billingCycle);

    if ($cycle === 'quarterly') {
        return $start->modify('+3 month')->modify('-1 day')->format('Y-m-d');
    }
    if ($cycle === 'annually') {
        return $start->modify('+1 year')->modify('-1 day')->format('Y-m-d');
    }
    return $start->modify('+1 month')->modify('-1 day')->format('Y-m-d');
}

function schoolErpNextPeriodStart(string $periodEndDate): string {
    $end = new DateTimeImmutable(schoolErpParseDate($periodEndDate) . ' 00:00:00', new DateTimeZone('UTC'));
    return $end->modify('+1 day')->format('Y-m-d');
}

function schoolErpApplyPaymentUrls(string $gateway, ?string $paymentLinkUrl): array {
    $primary = schoolErpPrimaryPayUrl();
    $gw = schoolErpNormalizeGateway($gateway);

    $paymentUrl = trim((string)$paymentLinkUrl);
    if ($paymentUrl === '' || $gw === 'pay_tezzcorp') {
        $paymentUrl = $primary;
    }

    // QR always points to the primary pay URL.
    return [
        'preferred_gateway' => $gw,
        'payment_url' => $paymentUrl,
        'qr_payment_url' => $primary,
    ];
}

function schoolErpGenerateInvoiceNumber(PDO $pdo, string $orgId): string {
    for ($i = 0; $i < 10; $i++) {
        $suffix = strtoupper(substr(bin2hex(random_bytes(3)), 0, 6));
        $candidate = 'SMI-' . gmdate('Ymd') . '-' . $suffix;
        $st = $pdo->prepare("
            SELECT 1
            FROM invoices
            WHERE organization_id = :org
              AND invoice_number = :invoice_number
              AND deleted_at IS NULL
            LIMIT 1
        ");
        $st->execute([
            ':org' => $orgId,
            ':invoice_number' => $candidate,
        ]);
        if (!$st->fetchColumn()) {
            return $candidate;
        }
    }

    return 'SMI-' . gmdate('YmdHis') . '-' . strtoupper(substr(bin2hex(random_bytes(2)), 0, 4));
}

function schoolErpGenerateDueMaintenanceBills(PDO $pdo, string $orgId, ?string $actorUserId = null, int $limit = 50, bool $notifyOwners = true): array {
    $limit = max(1, min($limit, 300));

    $summary = [
        'processed' => 0,
        'generated' => 0,
        'duplicates_skipped' => 0,
        'failed' => 0,
        'invoice_ids' => [],
        'bill_ids' => [],
    ];

    $seedSt = $pdo->prepare("
        SELECT c.id
        FROM erp_school_maintenance_contracts c
        JOIN erp_school_profiles s ON s.id = c.school_id
        WHERE c.organization_id = :org
          AND c.deleted_at IS NULL
          AND c.status = 'active'
          AND c.auto_generate_invoice = 1
          AND c.next_billing_date <= CURDATE()
          AND s.deleted_at IS NULL
        ORDER BY c.next_billing_date ASC, c.created_at ASC
        LIMIT :limit
    ");
    $seedSt->bindValue(':org', $orgId);
    $seedSt->bindValue(':limit', $limit, PDO::PARAM_INT);
    $seedSt->execute();
    $contractIds = $seedSt->fetchAll(PDO::FETCH_COLUMN) ?: [];

    foreach ($contractIds as $contractId) {
        $summary['processed']++;
        $contractId = (string)$contractId;
        if ($contractId === '') {
            continue;
        }

        $invoiceId = '';
        $billId = '';
        $accountId = '';
        $accountName = '';
        $issueDate = gmdate('Y-m-d');
        $cycle = 'monthly';
        $amount = 0.0;
        $costAmount = 0.0;
        $marginAmount = 0.0;
        $dueDate = gmdate('Y-m-d');
        $periodStart = gmdate('Y-m-d');
        $periodEnd = gmdate('Y-m-d');
        $gatewayMeta = schoolErpApplyPaymentUrls('pay_tezzcorp', null);

        try {
            $pdo->beginTransaction();

            $st = $pdo->prepare("
                SELECT
                  c.id,
                  c.school_id,
                  c.billing_cycle,
                  c.amount,
                  c.operational_cost_per_cycle,
                  c.currency,
                  c.payment_term_days,
                  c.next_billing_date,
                  c.preferred_gateway,
                  c.payment_link_url,
                  c.status,
                  c.deleted_at,
                  s.account_id,
                  s.school_code,
                  a.name AS account_name
                FROM erp_school_maintenance_contracts c
                JOIN erp_school_profiles s ON s.id = c.school_id
                JOIN accounts a ON a.id = s.account_id
                WHERE c.id = :id
                  AND c.organization_id = :org
                LIMIT 1
                FOR UPDATE
            ");
            $st->execute([
                ':id' => $contractId,
                ':org' => $orgId,
            ]);
            $contract = $st->fetch(PDO::FETCH_ASSOC);
            if (!$contract) {
                $pdo->rollBack();
                continue;
            }

            if ((string)$contract['deleted_at'] !== '' || (string)$contract['status'] !== 'active') {
                $pdo->rollBack();
                continue;
            }

            $periodStart = schoolErpParseDate((string)($contract['next_billing_date'] ?? ''), gmdate('Y-m-d'));
            $cycle = schoolErpNormalizeBillingCycle((string)($contract['billing_cycle'] ?? 'monthly'));
            $periodEnd = schoolErpPeriodEnd($periodStart, $cycle);
            $nextPeriodStart = schoolErpNextPeriodStart($periodEnd);

            $dupSt = $pdo->prepare("
                SELECT id
                FROM erp_school_maintenance_bills
                WHERE contract_id = :contract_id
                  AND billing_period_start = :period_start
                  AND billing_period_end = :period_end
                LIMIT 1
            ");
            $dupSt->execute([
                ':contract_id' => $contractId,
                ':period_start' => $periodStart,
                ':period_end' => $periodEnd,
            ]);
            $dupId = (string)($dupSt->fetchColumn() ?: '');
            if ($dupId !== '') {
                $upDup = $pdo->prepare("
                    UPDATE erp_school_maintenance_contracts
                    SET
                      last_billed_date = :last_billed_date,
                      next_billing_date = :next_billing_date,
                      updated_at = UTC_TIMESTAMP(3)
                    WHERE id = :id
                    LIMIT 1
                ");
                $upDup->execute([
                    ':last_billed_date' => $periodEnd,
                    ':next_billing_date' => $nextPeriodStart,
                    ':id' => $contractId,
                ]);
                $pdo->commit();
                $summary['duplicates_skipped']++;
                continue;
            }

            $amount = round((float)($contract['amount'] ?? 0), 2);
            if ($amount <= 0) {
                $pdo->rollBack();
                $summary['failed']++;
                continue;
            }

            $costAmount = round((float)($contract['operational_cost_per_cycle'] ?? 0), 2);
            if ($costAmount < 0) {
                $costAmount = 0.0;
            }
            $marginAmount = round($amount - $costAmount, 2);
            $currency = strtoupper(trim((string)($contract['currency'] ?? 'INR')));
            if ($currency === '' || strlen($currency) !== 3) {
                $currency = 'INR';
            }

            $termDays = max(0, min((int)($contract['payment_term_days'] ?? 7), 90));
            $issueDate = gmdate('Y-m-d');
            $dueDate = (new DateTimeImmutable($issueDate . ' 00:00:00', new DateTimeZone('UTC')))
                ->modify('+' . $termDays . ' day')
                ->format('Y-m-d');

            $accountId = (string)($contract['account_id'] ?? '');
            $accountName = (string)($contract['account_name'] ?? 'School Account');
            if ($accountId === '') {
                $pdo->rollBack();
                $summary['failed']++;
                continue;
            }

            $gatewayMeta = schoolErpApplyPaymentUrls((string)($contract['preferred_gateway'] ?? 'pay_tezzcorp'), (string)($contract['payment_link_url'] ?? ''));

            $invoiceId = uuid32();
            $invoiceNumber = schoolErpGenerateInvoiceNumber($pdo, $orgId);
            $invoiceNotes = 'ERP maintenance billing (' . $cycle . ') for ' . $accountName
                . ' | Period: ' . $periodStart . ' to ' . $periodEnd
                . ' | Pay via: ' . $gatewayMeta['payment_url'];

            $invSt = $pdo->prepare("
                INSERT INTO invoices
                  (id, organization_id, account_id, invoice_number, currency, issue_date, due_date, subtotal, tax, total, status, notes, created_at, updated_at, deleted_at)
                VALUES
                  (:id, :org, :account_id, :invoice_number, :currency, :issue_date, :due_date, :subtotal, 0.00, :total, 'sent', :notes, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3), NULL)
            ");
            $invSt->execute([
                ':id' => $invoiceId,
                ':org' => $orgId,
                ':account_id' => $accountId,
                ':invoice_number' => $invoiceNumber,
                ':currency' => $currency,
                ':issue_date' => $issueDate,
                ':due_date' => $dueDate,
                ':subtotal' => $amount,
                ':total' => $amount,
                ':notes' => $invoiceNotes,
            ]);

            $billId = uuid32();
            $billSt = $pdo->prepare("
                INSERT INTO erp_school_maintenance_bills
                  (id, organization_id, contract_id, school_id, billing_period_start, billing_period_end, due_date, amount, cost_amount, margin_amount, currency, invoice_id, payment_id, payment_status, payment_received_at, payment_reference, payment_method, preferred_gateway, payment_url, qr_payment_url, created_at, updated_at)
                VALUES
                  (:id, :org, :contract_id, :school_id, :period_start, :period_end, :due_date, :amount, :cost_amount, :margin_amount, :currency, :invoice_id, NULL, 'pending', NULL, NULL, NULL, :preferred_gateway, :payment_url, :qr_payment_url, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3))
            ");
            $billSt->execute([
                ':id' => $billId,
                ':org' => $orgId,
                ':contract_id' => $contractId,
                ':school_id' => (string)$contract['school_id'],
                ':period_start' => $periodStart,
                ':period_end' => $periodEnd,
                ':due_date' => $dueDate,
                ':amount' => $amount,
                ':cost_amount' => $costAmount,
                ':margin_amount' => $marginAmount,
                ':currency' => $currency,
                ':invoice_id' => $invoiceId,
                ':preferred_gateway' => $gatewayMeta['preferred_gateway'],
                ':payment_url' => $gatewayMeta['payment_url'],
                ':qr_payment_url' => $gatewayMeta['qr_payment_url'],
            ]);

            $upContract = $pdo->prepare("
                UPDATE erp_school_maintenance_contracts
                SET
                  last_billed_date = :last_billed_date,
                  next_billing_date = :next_billing_date,
                  updated_at = UTC_TIMESTAMP(3)
                WHERE id = :id
                LIMIT 1
            ");
            $upContract->execute([
                ':last_billed_date' => $periodEnd,
                ':next_billing_date' => $nextPeriodStart,
                ':id' => $contractId,
            ]);

            $pdo->commit();
            $summary['generated']++;
            $summary['invoice_ids'][] = $invoiceId;
            $summary['bill_ids'][] = $billId;
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            $summary['failed']++;
            error_log('[schoolErpGenerateDueMaintenanceBills] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
            continue;
        }

        if ($billId !== '') {
            if (function_exists('crmOpsAuditLog')) {
                crmOpsAuditLog($pdo, $orgId, $actorUserId, 'generate', 'erp_maintenance_bill', $billId, [], [
                    'contract_id' => $contractId,
                    'invoice_id' => $invoiceId,
                    'amount' => $amount,
                    'billing_cycle' => $cycle,
                    'period_start' => $periodStart,
                    'period_end' => $periodEnd,
                    'due_date' => $dueDate,
                    'payment_url' => $gatewayMeta['payment_url'],
                    'qr_payment_url' => $gatewayMeta['qr_payment_url'],
                ]);
            }
            if (function_exists('crmOpsWebhookEmit')) {
                crmOpsWebhookEmit($pdo, $orgId, 'erp.maintenance.bill_generated', 'erp_maintenance_bill', $billId, [
                    'contract_id' => $contractId,
                    'invoice_id' => $invoiceId,
                    'amount' => $amount,
                    'billing_cycle' => $cycle,
                    'period_start' => $periodStart,
                    'period_end' => $periodEnd,
                    'due_date' => $dueDate,
                ]);
            }
        }
    }

    if ($notifyOwners && $summary['generated'] > 0 && function_exists('crmOpsNotifyUsersByRole')) {
        $msg = $summary['generated'] . ' school maintenance bill(s) generated automatically.';
        crmOpsNotifyUsersByRole(
            $pdo,
            $orgId,
            ['Owner', 'Admin', 'Manager'],
            'School Billing Run Complete',
            $msg,
            'payment',
            'school-erp-bills',
            ['in_app']
        );
    }

    return $summary;
}
?>
