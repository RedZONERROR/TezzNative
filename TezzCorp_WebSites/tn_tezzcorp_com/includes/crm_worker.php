<?php
declare(strict_types=1);

require_once __DIR__ . '/crm_ops.php';

function crmWorkerFormatUtc(DateTimeImmutable $dt): string {
    return $dt
        ->setTimezone(new DateTimeZone('UTC'))
        ->format('Y-m-d H:i:s.v');
}

function crmWorkerTableExists(PDO $pdo, string $table): bool {
    static $cache = [];
    $table = trim($table);
    if ($table === '') {
        return false;
    }
    if (array_key_exists($table, $cache)) {
        return (bool)$cache[$table];
    }

    try {
        $st = $pdo->prepare("
            SELECT COUNT(*)
            FROM information_schema.TABLES
            WHERE TABLE_SCHEMA = DATABASE()
              AND TABLE_NAME = :table_name
        ");
        $st->execute([':table_name' => $table]);
        $cache[$table] = ((int)$st->fetchColumn() > 0);
    } catch (Throwable $e) {
        $cache[$table] = false;
    }

    return (bool)$cache[$table];
}

function crmWorkerCronPartMatch(int $value, string $expr, int $min, int $max): bool {
    $expr = trim($expr);
    if ($expr === '' || $expr === '*') {
        return true;
    }

    foreach (explode(',', $expr) as $tokenRaw) {
        $token = trim($tokenRaw);
        if ($token === '') {
            continue;
        }

        $step = 1;
        if (str_contains($token, '/')) {
            [$base, $stepRaw] = array_pad(explode('/', $token, 2), 2, '1');
            $token = trim($base);
            $step = max(1, (int)trim($stepRaw));
        }

        if ($token === '*') {
            if (($value - $min) % $step === 0) {
                return true;
            }
            continue;
        }

        if (str_contains($token, '-')) {
            [$startRaw, $endRaw] = array_pad(explode('-', $token, 2), 2, '');
            $start = max($min, min($max, (int)trim($startRaw)));
            $end = max($min, min($max, (int)trim($endRaw)));
            if ($start > $end) {
                [$start, $end] = [$end, $start];
            }
            if ($value >= $start && $value <= $end && (($value - $start) % $step === 0)) {
                return true;
            }
            continue;
        }

        $single = (int)$token;
        if ($single < $min || $single > $max) {
            continue;
        }
        if ($value === $single) {
            return true;
        }
    }

    return false;
}

function crmWorkerCronMatches(string $cron, DateTimeImmutable $dt): bool {
    $parts = preg_split('/\s+/', trim($cron)) ?: [];
    if (count($parts) !== 5) {
        return false;
    }

    [$mExpr, $hExpr, $domExpr, $monExpr, $dowExpr] = $parts;
    $minute = (int)$dt->format('i');
    $hour = (int)$dt->format('G');
    $day = (int)$dt->format('j');
    $month = (int)$dt->format('n');
    $dow = (int)$dt->format('w');

    return crmWorkerCronPartMatch($minute, $mExpr, 0, 59)
        && crmWorkerCronPartMatch($hour, $hExpr, 0, 23)
        && crmWorkerCronPartMatch($day, $domExpr, 1, 31)
        && crmWorkerCronPartMatch($month, $monExpr, 1, 12)
        && crmWorkerCronPartMatch($dow, $dowExpr, 0, 6);
}

function crmWorkerCronNextRun(string $cron, DateTimeImmutable $afterUtc): ?string {
    $cursor = $afterUtc
        ->setTimezone(new DateTimeZone('UTC'))
        ->setTime((int)$afterUtc->format('H'), (int)$afterUtc->format('i'), 0)
        ->modify('+1 minute');

    for ($i = 0; $i < 525600; $i++) {
        if (crmWorkerCronMatches($cron, $cursor)) {
            return crmWorkerFormatUtc($cursor);
        }
        $cursor = $cursor->modify('+1 minute');
    }
    return null;
}

function crmWorkerRunDueAutomationJobs(PDO $pdo, int $limit = 25, string $orgFilter = ''): array {
    $limit = max(1, min($limit, 200));
    $summary = ['processed' => 0, 'success' => 0, 'failed' => 0];

    $where = "status = 'active' AND (next_run_at IS NULL OR next_run_at <= UTC_TIMESTAMP(3))";
    $params = [];
    if ($orgFilter !== '') {
        $where .= " AND organization_id = :org";
        $params[':org'] = $orgFilter;
    }

    $st = $pdo->prepare("
        SELECT *
        FROM automation_jobs
        WHERE {$where}
        ORDER BY COALESCE(next_run_at, created_at) ASC
        LIMIT :limit
    ");
    foreach ($params as $k => $v) {
        $st->bindValue($k, $v);
    }
    $st->bindValue(':limit', $limit, PDO::PARAM_INT);
    $st->execute();
    $jobs = $st->fetchAll(PDO::FETCH_ASSOC) ?: [];

    foreach ($jobs as $job) {
        $summary['processed']++;
        $jobId = (string)$job['id'];
        $orgId = (string)$job['organization_id'];

        $runId = uuid32();
        $insRun = $pdo->prepare("
            INSERT INTO automation_job_runs (id, job_id, status, started_at, finished_at, output_text)
            VALUES (:id, :job_id, 'running', UTC_TIMESTAMP(3), NULL, NULL)
        ");
        $insRun->execute([
            ':id' => $runId,
            ':job_id' => $jobId,
        ]);

        $status = 'success';
        $result = [];
        try {
            $result = crmOpsRunAutomationJob($pdo, $orgId, $job);
            $summary['success']++;
            crmOpsWebhookEmit($pdo, $orgId, 'automation.job.ran', 'automation_job', $jobId, [
                'run_id' => $runId,
                'status' => 'success',
                'result' => $result,
            ]);
        } catch (Throwable $e) {
            $status = 'failed';
            $result = ['error' => $e->getMessage()];
            $summary['failed']++;
            crmOpsWebhookEmit($pdo, $orgId, 'automation.job.failed', 'automation_job', $jobId, [
                'run_id' => $runId,
                'status' => 'failed',
                'error' => $e->getMessage(),
            ]);
        }

        $upRun = $pdo->prepare("
            UPDATE automation_job_runs
            SET
              status = :status,
              finished_at = UTC_TIMESTAMP(3),
              output_text = :output
            WHERE id = :id
            LIMIT 1
        ");
        $upRun->execute([
            ':status' => $status,
            ':output' => json_encode($result, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE),
            ':id' => $runId,
        ]);

        $cron = trim((string)($job['schedule_cron'] ?? '0 10 * * *'));
        if ($cron === '') {
            $cron = '0 10 * * *';
        }
        $nextRunAt = crmWorkerCronNextRun($cron, new DateTimeImmutable('now', new DateTimeZone('UTC')));
        $upJob = $pdo->prepare("
            UPDATE automation_jobs
            SET
              last_run_at = UTC_TIMESTAMP(3),
              next_run_at = :next_run_at,
              updated_at = UTC_TIMESTAMP(3)
            WHERE id = :id
            LIMIT 1
        ");
        $upJob->execute([
            ':next_run_at' => $nextRunAt,
            ':id' => $jobId,
        ]);
    }

    return $summary;
}

function crmWorkerPersistHeartbeat(PDO $pdo, string $workerName, string $status, string $startedAt, string $finishedAt, array $summary): void {
    if (!crmWorkerTableExists($pdo, 'crm_worker_heartbeats')) {
        return;
    }
    $json = json_encode($summary, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($json === false) {
        $json = '{}';
    }

    try {
        $up = $pdo->prepare("
            INSERT INTO crm_worker_heartbeats
              (worker_name, last_seen_at, last_tick_started_at, last_tick_finished_at, last_status, last_summary_json, updated_at)
            VALUES
              (:worker_name, UTC_TIMESTAMP(3), :started_at, :finished_at, :status, :summary_json, UTC_TIMESTAMP(3))
            ON DUPLICATE KEY UPDATE
              last_seen_at = UTC_TIMESTAMP(3),
              last_tick_started_at = VALUES(last_tick_started_at),
              last_tick_finished_at = VALUES(last_tick_finished_at),
              last_status = VALUES(last_status),
              last_summary_json = VALUES(last_summary_json),
              updated_at = UTC_TIMESTAMP(3)
        ");
        $up->execute([
            ':worker_name' => $workerName,
            ':started_at' => $startedAt,
            ':finished_at' => $finishedAt,
            ':status' => $status,
            ':summary_json' => $json,
        ]);
    } catch (Throwable $e) {
        error_log('[crmWorkerPersistHeartbeat] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
    }
}

function crmWorkerPersistTick(PDO $pdo, string $workerName, string $mode, string $status, ?string $orgFilter, string $startedAt, string $finishedAt, int $durationMs, array $summary, ?string $errorText = null): void {
    if (!crmWorkerTableExists($pdo, 'crm_worker_ticks')) {
        return;
    }
    $json = json_encode($summary, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($json === false) {
        $json = '{}';
    }

    try {
        $ins = $pdo->prepare("
            INSERT INTO crm_worker_ticks
              (id, worker_name, run_mode, run_status, org_filter, started_at, finished_at, duration_ms, summary_json, error_text, created_at)
            VALUES
              (:id, :worker_name, :run_mode, :run_status, :org_filter, :started_at, :finished_at, :duration_ms, :summary_json, :error_text, UTC_TIMESTAMP(3))
        ");
        $ins->execute([
            ':id' => uuid32(),
            ':worker_name' => $workerName,
            ':run_mode' => $mode,
            ':run_status' => $status,
            ':org_filter' => $orgFilter !== '' ? $orgFilter : null,
            ':started_at' => $startedAt,
            ':finished_at' => $finishedAt,
            ':duration_ms' => max(0, $durationMs),
            ':summary_json' => $json,
            ':error_text' => $errorText !== null && trim($errorText) !== '' ? trim($errorText) : null,
        ]);
    } catch (Throwable $e) {
        error_log('[crmWorkerPersistTick] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
    }
}

function crmWorkerRunTick(PDO $pdo, int $limit = 25, string $orgFilter = '', string $mode = 'auto', string $workerName = 'main'): array {
    $tickStartDt = new DateTimeImmutable('now', new DateTimeZone('UTC'));
    $tickStart = crmWorkerFormatUtc($tickStartDt);
    $status = 'ok';
    $errorText = null;

    $summary = [
        'status' => 'ok',
        'mode' => $mode,
        'worker_name' => $workerName,
        'org_filter' => $orgFilter,
        'started_at' => $tickStart,
        'finished_at' => null,
        'duration_ms' => 0,
        'jobs' => ['processed' => 0, 'success' => 0, 'failed' => 0],
        'retries' => ['processed' => 0, 'sent' => 0, 'still_failed' => 0, 'terminal_failed' => 0, 'skipped' => 0],
        'webhooks' => ['processed' => 0, 'delivered' => 0, 'failed' => 0, 'rescheduled' => 0, 'skipped' => 0],
    ];

    try {
        $summary['jobs'] = crmWorkerRunDueAutomationJobs($pdo, $limit, $orgFilter);
        $summary['retries'] = crmOpsRetryFailedDeliveries($pdo, $limit);
        $summary['webhooks'] = crmOpsDispatchWebhookEvents($pdo, $limit);
    } catch (Throwable $e) {
        $status = 'error';
        $errorText = $e->getMessage();
        $summary['status'] = 'error';
        $summary['error'] = $errorText;
    }

    $tickEndDt = new DateTimeImmutable('now', new DateTimeZone('UTC'));
    $tickEnd = crmWorkerFormatUtc($tickEndDt);
    $durationMs = (int)round(((float)$tickEndDt->format('U.u') - (float)$tickStartDt->format('U.u')) * 1000);
    $summary['finished_at'] = $tickEnd;
    $summary['duration_ms'] = max(0, $durationMs);

    crmWorkerPersistTick($pdo, $workerName, $mode, $status, $orgFilter, $tickStart, $tickEnd, $summary['duration_ms'], $summary, $errorText);
    crmWorkerPersistHeartbeat($pdo, $workerName, $status, $tickStart, $tickEnd, $summary);

    return $summary;
}
