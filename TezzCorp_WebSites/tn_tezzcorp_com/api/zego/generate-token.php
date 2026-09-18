<?php
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET');
header('Access-Control-Allow-Headers: Content-Type');

$roomID = $_GET['roomID'] ?? 'vroom';
$userID = $_GET['userID'] ?? 'user_' . uniqid();
$userName = $_GET['userName'] ?? 'User_' . uniqid();
$appID = 1907566777; // From reference code
$serverSecret = '9b7cc6a8a0555a5671ca62d3c2949094'; // From reference code

try {
    $expireTs = time() + 3600; // Token valid for 1 hour
    $payload = [
        'room_id' => $roomID,
        'privilege' => [
            1 => 1, // Login room
            2 => 1  // Publish stream
        ],
        'secret' => true,
        'expire' => $expireTs
    ];
    $payloadStr = json_encode($payload);
    $iv = random_bytes(16);
    $encrypted = openssl_encrypt($payloadStr, 'AES-256-CBC', hex2bin($serverSecret), OPENSSL_RAW_DATA, $iv);
    if ($encrypted === false) {
        throw new Exception('Encryption failed: ' . openssl_error_string());
    }
    $token = '04' . base64_encode(pack('J', $appID) . pack('J', $expireTs) . $iv . $encrypted);
    echo json_encode(['token' => $token, 'userID' => $userID, 'userName' => $userName]);
} catch (Exception $e) {
    http_response_code(500);
    echo json_encode(['error' => 'Token generation failed: ' . $e->getMessage()]);
}
?>