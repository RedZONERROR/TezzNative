<?php
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST');
header('Access-Control-Allow-Headers: Content-Type');

// Log file for debugging
$logFile = 'call_log.txt';
$callFile = 'calls.json';

function logMessage($message) {
    global $logFile;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && !isset($_POST['action'])) {
    $data = json_decode(file_get_contents('php://input'), true);
    if (isset($data['callId'], $data['caller'], $data['recipient'])) {
        $calls = file_exists($callFile) ? json_decode(file_get_contents($callFile), true) : [];
        foreach ($calls as $callId => $call) {
            if ($call['recipient'] === $data['recipient'] || (time() - $call['timestamp'] > 600)) {
                unset($calls[$callId]);
            }
        }
        $calls[$data['callId']] = [
            'caller' => $data['caller'],
            'recipient' => $data['recipient'],
            'status' => 'pending',
            'timestamp' => time(),
            'joinLink' => $data['joinLink'] ?? ''
        ];
        if (!file_put_contents($callFile, json_encode($calls))) {
            logMessage("POST: Failed to write to calls.json for callId: {$data['callId']}");
            echo json_encode(['status' => 'error', 'message' => 'Failed to write to calls.json']);
            exit;
        }
        logMessage("POST: Successfully wrote callId: {$data['callId']} to calls.json");
        echo json_encode(['status' => 'success']);
    } else {
        logMessage("POST: Invalid data received");
        echo json_encode(['status' => 'error', 'message' => 'Invalid data']);
    }
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'GET' && isset($_GET['callId'])) {
    $callId = $_GET['callId'];
    logMessage("GET: Processing request for callId: $callId");
    $calls = file_exists($callFile) ? json_decode(file_get_contents($callFile), true) : [];
    if (isset($calls[$callId])) {
        if (time() - $calls[$callId]['timestamp'] > 600) {
            unset($calls[$callId]);
            if (!file_put_contents($callFile, json_encode($calls))) {
                logMessage("GET: Failed to write to calls.json after removing expired callId: $callId");
                echo json_encode(['status' => 'error', 'message' => 'Failed to write to calls.json']);
                exit;
            }
            logMessage("GET: Removed expired callId: $callId");
            echo json_encode(['status' => 'not_found']);
        } else {
            $callData = [
                'status' => $calls[$callId]['status'],
                'caller' => $calls[$callId]['caller'],
                'joinLink' => $calls[$callId]['joinLink']
            ];
            // Remove the call entry if it's pending or accepted
            if ($calls[$callId]['status'] === 'pending' || $calls[$callId]['status'] === 'accepted') {
                unset($calls[$callId]);
                if (!file_put_contents($callFile, json_encode($calls))) {
                    logMessage("GET: Failed to write to calls.json after removing callId: $callId, status: {$callData['status']}");
                    echo json_encode(['status' => 'error', 'message' => 'Failed to write to calls.json']);
                    exit;
                }
                logMessage("GET: Removed callId: $callId, status: {$callData['status']}");
            }
            echo json_encode($callData);
        }
    } else {
        logMessage("GET: CallId $callId not found");
        echo json_encode(['status' => 'not_found']);
    }
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['action'], $_POST['callId'])) {
    $callId = $_POST['callId'];
    $action = $_POST['action'];
    $calls = file_exists($callFile) ? json_decode(file_get_contents($callFile), true) : [];
    if (isset($calls[$callId])) {
        $calls[$callId]['status'] = $action === 'accept' ? 'accepted' : 'declined';
        $calls[$callId]['timestamp'] = time();
        if (!file_put_contents($callFile, json_encode($calls))) {
            logMessage("POST: Failed to write to calls.json for action: $action, callId: $callId");
            echo json_encode(['status' => 'error', 'message' => 'Failed to write to calls.json']);
            exit;
        }
        logMessage("POST: Updated callId: $callId to status: {$calls[$callId]['status']}");
        echo json_encode(['status' => 'success']);
    } else {
        logMessage("POST: CallId $callId not found for action: $action");
        echo json_encode(['status' => 'error', 'message' => 'Call not found']);
    }
    exit;
}

logMessage("Invalid request method or parameters");
echo json_encode(['status' => 'error', 'message' => 'Invalid request']);
?>