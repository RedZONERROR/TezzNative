<?php
declare(strict_types=1);

require_once __DIR__ . '/../config/config.php';

function tezzWebsiteActiveOrgId(PDO $pdo): ?string {
    static $cached = null;
    if ($cached !== null) {
        return $cached === '' ? null : $cached;
    }

    $host = strtolower((string)($_SERVER['HTTP_HOST'] ?? ''));
    $host = preg_replace('/:\\d+$/', '', $host);
    $host = preg_replace('/^www\./', '', $host);

    $stmt = $pdo->prepare(
        "SELECT id
         FROM organizations
         WHERE status = 'active'
         ORDER BY CASE WHEN domain = :domain THEN 0 ELSE 1 END, created_at ASC
         LIMIT 1"
    );
    $stmt->execute([':domain' => $host]);
    $orgId = (string)($stmt->fetchColumn() ?: '');
    $cached = $orgId;

    return $orgId !== '' ? $orgId : null;
}

function tezzWebsiteDecodeList($raw): array {
    if (is_array($raw)) {
        $out = [];
        foreach ($raw as $item) {
            $text = trim((string)$item);
            if ($text !== '') {
                $out[] = $text;
            }
        }
        return $out;
    }

    $rawText = trim((string)$raw);
    if ($rawText === '') {
        return [];
    }

    $decoded = json_decode($rawText, true);
    if (!is_array($decoded)) {
        return [];
    }

    $out = [];
    foreach ($decoded as $item) {
        $text = trim((string)$item);
        if ($text !== '') {
            $out[] = $text;
        }
    }

    return $out;
}

function tezzWebsiteFallbackServices(): array {
    return [
        [
            'name' => 'Website Engineering',
            'slug' => 'website-engineering',
            'short_description' => 'Conversion-focused websites with performance, accessibility, and SEO-ready structure.',
            'icon_class' => 'language',
            'badge' => 'Core Service',
            'highlights' => [
                'UI/UX architecture and responsive design',
                'CMS-ready content flows',
                'Deployment and release governance',
            ],
            'cta_label' => 'Discuss Service',
            'cta_url' => 'contact',
            'is_featured' => 1,
        ],
        [
            'name' => 'CRM Implementation',
            'slug' => 'crm-implementation',
            'short_description' => 'Client acquisition and retention workflows unified through configurable CRM operations.',
            'icon_class' => 'groups',
            'badge' => 'Revenue Ops',
            'highlights' => [
                'Lead-to-deal pipeline design',
                'Account, contact, and follow-up automation',
                'Sales and support reporting models',
            ],
            'cta_label' => 'Plan CRM Rollout',
            'cta_url' => 'contact',
            'is_featured' => 1,
        ],
        [
            'name' => 'API Product Development',
            'slug' => 'api-product-development',
            'short_description' => 'Production API stacks with usage controls, authentication strategy, and monetization readiness.',
            'icon_class' => 'hub',
            'badge' => 'Platform',
            'highlights' => [
                'API lifecycle and versioning',
                'Plan and subscription setup',
                'Monitoring, logs, and webhook support',
            ],
            'cta_label' => 'Launch API Product',
            'cta_url' => 'contact',
            'is_featured' => 1,
        ],
    ];
}

function tezzWebsiteFallbackProducts(): array {
    return [
        [
            'name' => 'TezzMeter',
            'slug' => 'tezzmeter',
            'category' => 'Desktop Utility',
            'tagline' => 'Real-time network speed monitoring. Made in India.',
            'description' => 'Lightweight desktop application for accurate upload/download monitoring with minimal resource usage.',
            'image_url' => 'TezzApps/TezzMeter/icon.png',
            'platform' => 'Windows / Linux / macOS',
            'features' => [
                'Live bandwidth meter',
                'Low resource usage',
                'Multi-platform installers',
            ],
            'cta_label' => 'Download Product',
            'cta_url' => 'TezzApps/TezzMeter/TezzCrop_SpeedMeter_Setup_V1.0.2.exe',
            'docs_url' => 'api-docs#tezzmeter',
            'is_featured' => 1,
        ],
        [
            'name' => 'Tezz CRM Control Tower',
            'slug' => 'crm-control-tower',
            'category' => 'CRM Platform',
            'tagline' => 'Pipeline, invoicing, and service operations in one console.',
            'description' => 'Production CRM built for growth teams to manage leads, deals, invoices, payments, and support workflows.',
            'image_url' => 'img/tezz-corp-logo.png',
            'platform' => 'Web SaaS',
            'features' => [
                'Multi-team workflow model',
                'Role-based permissions',
                'Revenue and operations dashboards',
            ],
            'cta_label' => 'Open CRM',
            'cta_url' => 'login',
            'docs_url' => 'api-docs',
            'is_featured' => 1,
        ],
    ];
}

function tezzWebsiteFallbackCaseStudies(): array {
    return [
        [
            'title' => 'Faster Lead-to-Deal Pipeline',
            'slug' => 'saas-pipeline-acceleration',
            'client_name' => 'B2B SaaS Client',
            'industry' => 'B2B SaaS',
            'category' => 'CRM Automation',
            'summary' => 'Implemented CRM automation with qualification scoring and deal routing for quicker conversion cycles.',
            'challenge_text' => 'Manual qualification and inconsistent follow-up slowed conversion.',
            'solution_text' => 'Built automated lead routing, SLA alerts, and stage-level analytics dashboards.',
            'impact_label' => 'Cycle Time',
            'impact_value' => '-32%',
            'technologies' => ['PHP', 'MySQL', 'Workflow Automation', 'Dashboarding'],
            'image_url' => 'img/app.png',
            'cta_label' => 'View Approach',
            'cta_url' => 'contact',
            'is_featured' => 1,
        ],
        [
            'title' => 'High-Availability Website Stack',
            'slug' => 'high-availability-web-stack',
            'client_name' => 'D2C Platform',
            'industry' => 'D2C',
            'category' => 'Website Engineering',
            'summary' => 'Re-architected frontend and deployment process to handle campaign traffic with stable performance.',
            'challenge_text' => 'Traffic spikes during campaigns caused downtime and checkout drops.',
            'solution_text' => 'Introduced performance-first frontend, deployment checkpoints, and active monitoring.',
            'impact_label' => 'Uptime',
            'impact_value' => '99.9%',
            'technologies' => ['Performance Engineering', 'CDN', 'Release Governance'],
            'image_url' => 'img/web.png',
            'cta_label' => 'View Stack',
            'cta_url' => 'contact',
            'is_featured' => 1,
        ],
    ];
}

function tezzWebsiteFallbackPlans(): array {
    return [
        [
            'name' => 'Build Sprint',
            'slug' => 'build-sprint',
            'best_for' => 'Teams launching a new website or product module',
            'price_label' => 'Custom Scope',
            'billing_cycle' => 'Project Based',
            'description' => 'Discovery-to-launch engagement for focused product delivery.',
            'includes' => ['Discovery', 'UI/UX', 'Development', 'QA', 'Production release'],
            'support_text' => '30-day stabilization window',
            'cta_label' => 'Request Proposal',
            'cta_url' => 'contact',
            'is_popular' => 0,
        ],
        [
            'name' => 'Growth Stack',
            'slug' => 'growth-stack',
            'best_for' => 'Businesses scaling CRM, website, and automation together',
            'price_label' => 'Monthly Retainer',
            'billing_cycle' => 'Monthly',
            'description' => 'Cross-functional delivery squad for growth and operational velocity.',
            'includes' => ['Delivery squad', 'Workflow automation', 'Dashboarding', 'Integrations'],
            'support_text' => 'Monthly optimization and reporting',
            'cta_label' => 'Talk To Sales',
            'cta_url' => 'contact',
            'is_popular' => 1,
        ],
    ];
}

function tezzWebsiteFetchServices(PDO $pdo, ?string $orgId, int $limit = 0): array {
    if ($orgId === null || $orgId === '') {
        return tezzWebsiteFallbackServices();
    }

    try {
        $sql = "SELECT id, name, slug, short_description, details, icon_class, badge, highlights_json, cta_label, cta_url, sort_order, is_featured, status
                FROM website_services
                WHERE organization_id = :org AND status = 'published' AND deleted_at IS NULL
                ORDER BY is_featured DESC, sort_order ASC, created_at DESC";
        if ($limit > 0) {
            $sql .= ' LIMIT ' . (int)$limit;
        }

        $stmt = $pdo->prepare($sql);
        $stmt->execute([':org' => $orgId]);
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

        $out = [];
        foreach ($rows as $row) {
            $row['highlights'] = tezzWebsiteDecodeList($row['highlights_json'] ?? '');
            $row['is_featured'] = (int)($row['is_featured'] ?? 0);
            $out[] = $row;
        }

        return $out;
    } catch (Throwable $e) {
        error_log('[website_content.services] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return tezzWebsiteFallbackServices();
    }
}

function tezzWebsiteFetchProducts(PDO $pdo, ?string $orgId, int $limit = 0): array {
    if ($orgId === null || $orgId === '') {
        return tezzWebsiteFallbackProducts();
    }

    try {
        $sql = "SELECT id, name, slug, category, tagline, description, image_url, platform, features_json, cta_label, cta_url, docs_url, sort_order, is_featured, status
                FROM website_products
                WHERE organization_id = :org AND status = 'published' AND deleted_at IS NULL
                ORDER BY is_featured DESC, sort_order ASC, created_at DESC";
        if ($limit > 0) {
            $sql .= ' LIMIT ' . (int)$limit;
        }

        $stmt = $pdo->prepare($sql);
        $stmt->execute([':org' => $orgId]);
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

        $out = [];
        foreach ($rows as $row) {
            $row['features'] = tezzWebsiteDecodeList($row['features_json'] ?? '');
            $row['is_featured'] = (int)($row['is_featured'] ?? 0);
            $out[] = $row;
        }

        return $out;
    } catch (Throwable $e) {
        error_log('[website_content.products] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return tezzWebsiteFallbackProducts();
    }
}

function tezzWebsiteFetchCaseStudies(PDO $pdo, ?string $orgId, int $limit = 0): array {
    if ($orgId === null || $orgId === '') {
        return tezzWebsiteFallbackCaseStudies();
    }

    try {
        $sql = "SELECT id, title, slug, client_name, industry, category, summary, challenge_text, solution_text, impact_label, impact_value, technologies_json, image_url, cta_label, cta_url, sort_order, is_featured, status
                FROM website_case_studies
                WHERE organization_id = :org AND status = 'published' AND deleted_at IS NULL
                ORDER BY is_featured DESC, sort_order ASC, created_at DESC";
        if ($limit > 0) {
            $sql .= ' LIMIT ' . (int)$limit;
        }

        $stmt = $pdo->prepare($sql);
        $stmt->execute([':org' => $orgId]);
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

        $out = [];
        foreach ($rows as $row) {
            $row['technologies'] = tezzWebsiteDecodeList($row['technologies_json'] ?? '');
            $row['is_featured'] = (int)($row['is_featured'] ?? 0);
            $out[] = $row;
        }

        return $out;
    } catch (Throwable $e) {
        error_log('[website_content.case_studies] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return tezzWebsiteFallbackCaseStudies();
    }
}

function tezzWebsiteFetchPlans(PDO $pdo, ?string $orgId, int $limit = 0): array {
    if ($orgId === null || $orgId === '') {
        return tezzWebsiteFallbackPlans();
    }

    try {
        $sql = "SELECT id, name, slug, best_for, price_label, billing_cycle, description, includes_json, support_text, cta_label, cta_url, sort_order, is_popular, status
                FROM website_service_plans
                WHERE organization_id = :org AND status = 'published' AND deleted_at IS NULL
                ORDER BY is_popular DESC, sort_order ASC, created_at DESC";
        if ($limit > 0) {
            $sql .= ' LIMIT ' . (int)$limit;
        }

        $stmt = $pdo->prepare($sql);
        $stmt->execute([':org' => $orgId]);
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC) ?: [];

        $out = [];
        foreach ($rows as $row) {
            $row['includes'] = tezzWebsiteDecodeList($row['includes_json'] ?? '');
            $row['is_popular'] = (int)($row['is_popular'] ?? 0);
            $out[] = $row;
        }

        return $out;
    } catch (Throwable $e) {
        error_log('[website_content.plans] ' . $e->getMessage() . PHP_EOL, 3, LOG_PATH . '/error.log');
        return tezzWebsiteFallbackPlans();
    }
}
?>
