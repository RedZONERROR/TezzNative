<?php
ini_set('display_errors', 1);
error_reporting(E_ALL);

require_once __DIR__ . '/config/config.php';
echo "[START] Deploying ERP Schema...<br>\n";

try {
    $pdo = getDBConnection();
} catch (Throwable $e) {
    die("[ERR] DB Connection: " . $e->getMessage());
}

$sqlFile = __DIR__ . '/crm.tezzcorp.com/sql/erp_schema.sql';
if (!file_exists($sqlFile)) {
    die("[ERR] Schema file not found at " . $sqlFile);
}

$sql = file_get_contents($sqlFile);

// Quick split by ;
$statements = array_filter(
    array_map('trim', explode(';', $sql)),
    fn($s) => !empty($s)
);

try {
    foreach ($statements as $stmt) {
        // Skip pure comments or empty remaining after trim
        if (substr($stmt, 0, 2) === '--' || empty($stmt)) continue;
        $pdo->exec($stmt);
    }
    
    // Seed default plan
    $row = $pdo->query("SELECT id FROM organizations LIMIT 1")->fetch();
    if ($row) {
        $orgId = $row['id'];
        $check = $pdo->query("SELECT COUNT(*) FROM erp_plans WHERE organization_id = '{$orgId}'")->fetchColumn();
        if ((int)$check === 0) {
            $planId = uuid32();
            $pdo->exec("INSERT INTO erp_plans (id, organization_id, name, price_monthly, price_yearly, max_students, max_staff, features_json) 
                        VALUES ('{$planId}', '{$orgId}', 'Enterprise Master', 5000.00, 50000.00, 0, 0, '\"all\"')");
            echo "[OK] Seeded default Enterprise Master plan<br>\n";
        }
    }

    echo "[OK] ERP Tables Created/Verified successfully!<br>\n";
} catch (Throwable $e) {
    die("[ERR] Execution failed: " . $e->getMessage());
}
?>
