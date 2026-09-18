<!DOCTYPE html>
<html lang="en-IN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>API Documentation - TezzCorp</title>
    <meta name="description" content="Comprehensive documentation for TezzCorp Biometric API. Learn how to integrate our biometric attendance system into your applications.">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;500;600;700;800&family=Sora:wght@500;600;700;800&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.7.0/styles/atom-one-dark.min.css">
    <link rel="stylesheet" href="css/style.css?v=<?php echo time(); ?>">
    <!--<link rel="stylesheet" href="css/footer.css">-->
    <link rel="stylesheet" href="css/api-docs.css?v=<?php echo time(); ?>">
    <!-- Favicon -->
    <link rel="apple-touch-icon" sizes="180x180" href="favicon/apple-touch-icon.png">
    <link rel="icon" type="image/png" sizes="32x32" href="favicon/favicon-32x32.png">
    <link rel="icon" type="image/png" sizes="16x16" href="favicon/favicon-16x16.png">
    <link rel="manifest" href="favicon/site.webmanifest">
</head>
<body class="light-mode page-api-docs">
    
    <?php include 'header.php'; ?>

    <!-- Page Banner -->
    <section class="page-banner">
        <div class="banner-shape"></div>
        <div class="container">
            <div class="banner-content">
                <h1>API <span class="dynamic-gradient-text">Documentation</span></h1>
                <p>Comprehensive guide to our Biometric API</p>
            </div>
        </div>
    </section>

    <!-- API Documentation -->
    <section class="api-docs-section">
        <div class="container">
            <div class="docs-container">
                <!-- Sidebar -->
                <div class="docs-sidebar">
                    <div class="sidebar-header">
                        <h3>API Documentation</h3>
                    </div>
                    <div class="sidebar-search">
                        <input type="text" placeholder="Search..." id="docs-search">
                        <i class="material-icons" style="font-size:1.1rem;position:absolute;left:12px;top:50%;transform:translateY(-50%);opacity:0.5">search</i>
                    </div>
                    <ul class="sidebar-menu">
                        <li><a href="#introduction" class="active">Introduction</a></li>
                        <li><a href="#authentication">Authentication</a></li>
                        <li><a href="#endpoints">Endpoints</a>
                            <ul class="submenu">
                                <li><a href="#users">Users</a></li>
                                <li><a href="#devices">Devices</a></li>
                                <li><a href="#attendance">Attendance</a></li>
                                <li><a href="#reports">Reports</a></li>
                            </ul>
                        </li>
                        <li><a href="#errors">Error Handling</a></li>
                        <li><a href="#rate-limits">Rate Limits</a></li>
                        <li><a href="#webhooks">Webhooks</a></li>
                        <li><a href="#sdks">SDKs & Libraries</a></li>
                        <li><a href="#examples">Code Examples</a></li>
                        <li><a href="#changelog">Changelog</a></li>
                    </ul>
                </div>

                <!-- Content -->
                <div class="docs-content">
                    <div class="content-section" id="introduction">
                        <h2>Introduction</h2>
                        <p>The TezzCorp Biometric API allows you to integrate our biometric attendance system into your applications. With our API, you can manage users, devices, and attendance records, and generate reports.</p>
                        
                        <h3>Base URL</h3>
                        <div class="code-block">
                            <pre><code>https://api.tezzcorp.com/v1</code></pre>
                        </div>
                        
                        <h3>API Versions</h3>
                        <p>The current version of the API is v1. We recommend specifying the API version in the URL to ensure compatibility with your application.</p>
                        
                        <div class="info-box">
                            <div class="info-icon"><i class="material-icons" style="font-size:1.4rem">info</i></div>
                            <div class="info-content">
                                <p>All API requests must be made over HTTPS. Calls made over plain HTTP will fail.</p>
                            </div>
                        </div>
                    </div>

                    <div class="content-section" id="authentication">
                        <h2>Authentication</h2>
                        <p>The Biometric API uses API keys to authenticate requests. You can view and manage your API keys in the <a href="login">Dashboard</a>.</p>
                        
                        <p>Authentication is performed via HTTP Bearer Auth. Provide your API key as the bearer token value.</p>
                        
                        <h3>Example Request</h3>
                        <div class="code-block">
                            <pre><code class="language-bash">curl -X GET "https://api.tezzcorp.com/v1/users" \
-H "Authorization: Bearer YOUR_API_KEY"</code></pre>
                        </div>
                        
                        <div class="warning-box">
                            <div class="warning-icon"><i class="material-icons" style="font-size:1.4rem">warning</i></div>
                            <div class="warning-content">
                                <p>Keep your API keys secure! Do not share them in publicly accessible areas such as GitHub, client-side code, etc.</p>
                            </div>
                        </div>
                    </div>

                    <div class="content-section" id="endpoints">
                        <h2>Endpoints</h2>
                        <p>The API provides the following endpoints for managing your biometric attendance system:</p>
                        
                        <div class="content-section" id="users">
                            <h3>Users</h3>
                            <p>Endpoints for managing users in your biometric system.</p>
                            
                            <div class="endpoint">
                                <div class="endpoint-header">
                                    <span class="method get">GET</span>
                                    <span class="path">/users</span>
                                </div>
                                <div class="endpoint-description">
                                    <p>Returns a list of all users.</p>
                                    
                                    <h4>Query Parameters</h4>
                                    <table class="params-table">
                                        <thead>
                                            <tr>
                                                <th>Parameter</th>
                                                <th>Type</th>
                                                <th>Description</th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <tr>
                                                <td>limit</td>
                                                <td>integer</td>
                                                <td>Maximum number of users to return. Default is 20.</td>
                                            </tr>
                                            <tr>
                                                <td>offset</td>
                                                <td>integer</td>
                                                <td>Number of users to skip. Default is 0.</td>
                                            </tr>
                                        </tbody>
                                    </table>
                                    
                                    <h4>Example Request</h4>
                                    <div class="code-block">
                                        <pre><code class="language-bash">curl -X GET "https://api.tezzcorp.com/v1/users?limit=10&offset=0" \
-H "Authorization: Bearer YOUR_API_KEY"</code></pre>
                                    </div>
                                    
                                    <h4>Example Response</h4>
                                    <div class="code-block">
                                        <pre><code class="language-json">{
  "data": [
    {
      "id": "usr_123456789",
      "name": "John Doe",
      "email": "john.doe@example.com",
      "phone": "+919876543210",
      "department": "IT",
      "created_at": "2023-01-15T10:30:00Z",
      "updated_at": "2023-01-15T10:30:00Z"
    },
    {
      "id": "usr_987654321",
      "name": "Jane Smith",
      "email": "jane.smith@example.com",
      "phone": "+919876543211",
      "department": "HR",
      "created_at": "2023-01-16T11:45:00Z",
      "updated_at": "2023-01-16T11:45:00Z"
    }
  ],
  "meta": {
    "total": 45,
    "limit": 10,
    "offset": 0
  }
}</code></pre>
                                    </div>
                                </div>
                            </div>
                            
                            <div class="endpoint">
                                <div class="endpoint-header">
                                    <span class="method post">POST</span>
                                    <span class="path">/users</span>
                                </div>
                                <div class="endpoint-description">
                                    <p>Creates a new user.</p>
                                    
                                    <h4>Request Body</h4>
                                    <table class="params-table">
                                        <thead>
                                            <tr>
                                                <th>Parameter</th>
                                                <th>Type</th>
                                                <th>Description</th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <tr>
                                                <td>name</td>
                                                <td>string</td>
                                                <td>Required. The user's full name.</td>
                                            </tr>
                                            <tr>
                                                <td>email</td>
                                                <td>string</td>
                                                <td>Required. The user's email address.</td>
                                            </tr>
                                            <tr>
                                                <td>phone</td>
                                                <td>string</td>
                                                <td>Required. The user's phone number.</td>
                                            </tr>
                                            <tr>
                                                <td>department</td>
                                                <td>string</td>
                                                <td>Optional. The user's department.</td>
                                            </tr>
                                        </tbody>
                                    </table>
                                    
                                    <h4>Example Request</h4>
                                    <div class="code-block">
                                        <pre><code class="language-bash">curl -X POST "https://api.tezzcorp.com/v1/users" \
-H "Authorization: Bearer YOUR_API_KEY" \
-H "Content-Type: application/json" \
-d '{
  "name": "John Doe",
  "email": "john.doe@example.com",
  "phone": "+919876543210",
  "department": "IT"
}'</code></pre>
                                    </div>
                                    
                                    <h4>Example Response</h4>
                                    <div class="code-block">
                                        <pre><code class="language-json">{
  "data": {
    "id": "usr_123456789",
    "name": "John Doe",
    "email": "john.doe@example.com",
    "phone": "+919876543210",
    "department": "IT",
    "created_at": "2023-01-15T10:30:00Z",
    "updated_at": "2023-01-15T10:30:00Z"
  }
}</code></pre>
                                    </div>
                                </div>
                            </div>
                            
                            <!-- More user endpoints would go here -->
                        </div>
                        
                        <div class="content-section" id="devices">
                            <h3>Devices</h3>
                            <p>Endpoints for managing biometric devices.</p>
                            
                            <div class="endpoint">
                                <div class="endpoint-header">
                                    <span class="method get">GET</span>
                                    <span class="path">/devices</span>
                                </div>
                                <div class="endpoint-description">
                                    <p>Returns a list of all registered devices.</p>
                                    
                                    <h4>Example Request</h4>
                                    <div class="code-block">
                                        <pre><code class="language-bash">curl -X GET "https://api.tezzcorp.com/v1/devices" \
-H "Authorization: Bearer YOUR_API_KEY"</code></pre>
                                    </div>
                                    
                                    <h4>Example Response</h4>
                                    <div class="code-block">
                                        <pre><code class="language-json">{
  "data": [
    {
      "id": "dev_123456789",
      "name": "Main Entrance",
      "serial_number": "BIO-FP-001",
      "type": "fingerprint",
      "status": "active",
      "location": "Office Building - Ground Floor",
      "last_sync": "2023-01-15T10:30:00Z",
      "created_at": "2023-01-01T09:00:00Z",
      "updated_at": "2023-01-15T10:30:00Z"
    },
    {
      "id": "dev_987654321",
      "name": "Back Entrance",
      "serial_number": "BIO-FP-002",
      "type": "fingerprint",
      "status": "active",
      "location": "Office Building - Back Door",
      "last_sync": "2023-01-15T10:35:00Z",
      "created_at": "2023-01-01T09:15:00Z",
      "updated_at": "2023-01-15T10:35:00Z"
    }
  ]
}</code></pre>
                                    </div>
                                </div>
                            </div>
                            
                            <!-- More device endpoints would go here -->
                        </div>
                        
                        <div class="content-section" id="attendance">
                            <h3>Attendance</h3>
                            <p>Endpoints for managing attendance records.</p>
                            
                            <div class="endpoint">
                                <div class="endpoint-header">
                                    <span class="method post">POST</span>
                                    <span class="path">/attendance</span>
                                </div>
                                <div class="endpoint-description">
                                    <p>Records a new attendance entry.</p>
                                    
                                    <h4>Request Body</h4>
                                    <table class="params-table">
                                        <thead>
                                            <tr>
                                                <th>Parameter</th>
                                                <th>Type</th>
                                                <th>Description</th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <tr>
                                                <td>user_id</td>
                                                <td>string</td>
                                                <td>Required. The ID of the user.</td>
                                            </tr>
                                            <tr>
                                                <td>device_id</td>
                                                <td>string</td>
                                                <td>Required. The ID of the device.</td>
                                            </tr>
                                            <tr>
                                                <td>type</td>
                                                <td>string</td>
                                                <td>Required. Either "check_in" or "check_out".</td>
                                            </tr>
                                            <tr>
                                                <td>timestamp</td>
                                                <td>string</td>
                                                <td>Optional. ISO 8601 formatted timestamp. Defaults to current time.</td>
                                            </tr>
                                        </tbody>
                                    </table>
                                    
                                    <h4>Example Request</h4>
                                    <div class="code-block">
                                        <pre><code class="language-bash">curl -X POST "https://api.tezzcorp.com/v1/attendance" \
-H "Authorization: Bearer YOUR_API_KEY" \
-H "Content-Type: application/json" \
-d '{
  "user_id": "usr_123456789",
  "device_id": "dev_123456789",
  "type": "check_in",
  "timestamp": "2023-01-15T09:00:00Z"
}'</code></pre>
                                    </div>
                                    
                                    <h4>Example Response</h4>
                                    <div class="code-block">
                                        <pre><code class="language-json">{
  "data": {
    "id": "att_123456789",
    "user_id": "usr_123456789",
    "device_id": "dev_123456789",
    "type": "check_in",
    "timestamp": "2023-01-15T09:00:00Z",
    "created_at": "2023-01-15T09:00:05Z"
  }
}</code></pre>
                                    </div>
                                </div>
                            </div>
                            
                            <!-- More attendance endpoints would go here -->
                        </div>
                        
                        <div class="content-section" id="reports">
                            <h3>Reports</h3>
                            <p>Endpoints for generating attendance reports.</p>
                            
                            <div class="endpoint">
                                <div class="endpoint-header">
                                    <span class="method get">GET</span>
                                    <span class="path">/reports/attendance</span>
                                </div>
                                <div class="endpoint-description">
                                    <p>Generates an attendance report for a specified time period.</p>
                                    
                                    <h4>Query Parameters</h4>
                                    <table class="params-table">
                                        <thead>
                                            <tr>
                                                <th>Parameter</th>
                                                <th>Type</th>
                                                <th>Description</th>
                                            </tr>
                                        </thead>
                                        <tbody>
                                            <tr>
                                                <td>start_date</td>
                                                <td>string</td>
                                                <td>Required. Start date in YYYY-MM-DD format.</td>
                                            </tr>
                                            <tr>
                                                <td>end_date</td>
                                                <td>string</td>
                                                <td>Required. End date in YYYY-MM-DD format.</td>
                                            </tr>
                                            <tr>
                                                <td>user_id</td>
                                                <td>string</td>
                                                <td>Optional. Filter by user ID.</td>
                                            </tr>
                                            <tr>
                                                <td>department</td>
                                                <td>string</td>
                                                <td>Optional. Filter by department.</td>
                                            </tr>
                                            <tr>
                                                <td>format</td>
                                                <td>string</td>
                                                <td>Optional. Response format: "json" (default), "csv", or "pdf".</td>
                                            </tr>
                                        </tbody>
                                    </table>
                                    
                                    <h4>Example Request</h4>
                                    <div class="code-block">
                                        <pre><code class="language-bash">curl -X GET "https://api.tezzcorp.com/v1/reports/attendance?start_date=2023-01-01&end_date=2023-01-31&department=IT" \
-H "Authorization: Bearer YOUR_API_KEY"</code></pre>
                                    </div>
                                    
                                    <h4>Example Response</h4>
                                    <div class="code-block">
                                        <pre><code class="language-json">{
  "data": {
    "report_id": "rep_123456789",
    "start_date": "2023-01-01",
    "end_date": "2023-01-31",
    "department": "IT",
    "generated_at": "2023-02-01T10:00:00Z",
    "summary": {
      "total_users": 15,
      "total_days": 31,
      "working_days": 22,
      "average_attendance": 95.5
    },
    "users": [
      {
        "id": "usr_123456789",
        "name": "John Doe",
        "department": "IT",
        "attendance": {
          "present_days": 21,
          "absent_days": 1,
          "late_days": 2,
          "early_departure_days": 1,
          "attendance_percentage": 95.45
        }
      },
      // More users...
    ]
  }
}</code></pre>
                                    </div>
                                </div>
                            </div>
                            
                            <!-- More report endpoints would go here -->
                        </div>
                    </div>

                    <div class="content-section" id="errors">
                        <h2>Error Handling</h2>
                        <p>The API uses conventional HTTP response codes to indicate the success or failure of an API request.</p>
                        
                        <table class="params-table">
                            <thead>
                                <tr>
                                    <th>Code</th>
                                    <th>Description</th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr>
                                    <td>200 - OK</td>
                                    <td>Everything worked as expected.</td>
                                </tr>
                                <tr>
                                    <td>400 - Bad Request</td>
                                    <td>The request was unacceptable, often due to missing a required parameter.</td>
                                </tr>
                                <tr>
                                    <td>401 - Unauthorized</td>
                                    <td>No valid API key provided.</td>
                                </tr>
                                <tr>
                                    <td>403 - Forbidden</td>
                                    <td>The API key doesn't have permissions to perform the request.</td>
                                </tr>
                                <tr>
                                    <td>404 - Not Found</td>
                                    <td>The requested resource doesn't exist.</td>
                                </tr>
                                <tr>
                                    <td>429 - Too Many Requests</td>
                                    <td>Too many requests hit the API too quickly.</td>
                                </tr>
                                <tr>
                                    <td>500, 502, 503, 504 - Server Errors</td>
                                    <td>Something went wrong on our end.</td>
                                </tr>
                            </tbody>
                        </table>
                        
                        <h3>Error Response Format</h3>
                        <div class="code-block">
                            <pre><code class="language-json">{
  "error": {
    "code": "invalid_request",
    "message": "The request was unacceptable, often due to missing a required parameter.",
    "param": "user_id",
    "type": "validation_error"
  }
}</code></pre>
                        </div>
                    </div>

                    <div class="content-section" id="rate-limits">
                        <h2>Rate Limits</h2>
                        <p>The API has rate limits to prevent abuse and ensure stability. The current rate limits are:</p>
                        
                        <table class="params-table">
                            <thead>
                                <tr>
                                    <th>Plan</th>
                                    <th>Rate Limit</th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr>
                                    <td>Basic</td>
                                    <td>100 requests per minute</td>
                                </tr>
                                <tr>
                                    <td>Premium</td>
                                    <td>500 requests per minute</td>
                                </tr>
                                <tr>
                                    <td>Enterprise</td>
                                    <td>1000 requests per minute</td>
                                </tr>
                            </tbody>
                        </table>
                        
                        <p>Rate limit information is included in the response headers:</p>
                        <ul>
                            <li><code>X-RateLimit-Limit</code>: The maximum number of requests you're permitted to make per minute.</li>
                            <li><code>X-RateLimit-Remaining</code>: The number of requests remaining in the current rate limit window.</li>
                            <li><code>X-RateLimit-Reset</code>: The time at which the current rate limit window resets in UTC epoch seconds.</li>
                        </ul>
                    </div>

                    <div class="content-section" id="webhooks">
                        <h2>Webhooks</h2>
                        <p>Webhooks allow you to receive real-time notifications when events occur in your biometric system.</p>
                        
                        <h3>Available Events</h3>
                        <table class="params-table">
                            <thead>
                                <tr>
                                    <th>Event</th>
                                    <th>Description</th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr>
                                    <td>user.created</td>
                                    <td>Triggered when a new user is created.</td>
                                </tr>
                                <tr>
                                    <td>user.updated</td>
                                    <td>Triggered when a user is updated.</td>
                                </tr>
                                <tr>
                                    <td>attendance.created</td>
                                    <td>Triggered when a new attendance record is created.</td>
                                </tr>
                                <tr>
                                    <td>device.status_changed</td>
                                    <td>Triggered when a device's status changes.</td>
                                </tr>
                            </tbody>
                        </table>
                        
                        <h3>Setting Up Webhooks</h3>
                        <p>You can set up webhooks in the <a href="login">Dashboard</a> under the Webhooks section.</p>
                        
                        <h3>Webhook Payload</h3>
                        <div class="code-block">
                            <pre><code class="language-json">{
  "id": "evt_123456789",
  "type": "attendance.created",
  "created_at": "2023-01-15T09:00:05Z",
  "data": {
    "id": "att_123456789",
    "user_id": "usr_123456789",
    "device_id": "dev_123456789",
    "type": "check_in",
    "timestamp": "2023-01-15T09:00:00Z"
  }
}</code></pre>
                        </div>
                    </div>

                    <div class="content-section" id="sdks">
                        <h2>SDKs & Libraries</h2>
                        <p>We provide official SDKs for the following languages:</p>
                        
                        <div class="sdk-list">
                            <div class="sdk-item">
                                <div class="sdk-icon"><i class="material-icons">code</i></div>
                                <div class="sdk-info">
                                    <h3>PHP</h3>
                                    <p>Our PHP SDK makes it easy to integrate with the Biometric API in your PHP applications.</p>
                                    <a href="#" class="btn-sdk">View on GitHub</a>
                                </div>
                            </div>
                            
                            <div class="sdk-item">
                                <div class="sdk-icon"><i class="material-icons">terminal</i></div>
                                <div class="sdk-info">
                                    <h3>JavaScript</h3>
                                    <p>Our JavaScript SDK works in both Node.js and browser environments.</p>
                                    <a href="#" class="btn-sdk">View on GitHub</a>
                                </div>
                            </div>
                            
                            <div class="sdk-item">
                                <div class="sdk-icon"><i class="material-icons">settings</i></div>
                                <div class="sdk-info">
                                    <h3>Python</h3>
                                    <p>Our Python SDK is compatible with Python 3.6+ and includes async support.</p>
                                    <a href="#" class="btn-sdk">View on GitHub</a>
                                </div>
                            </div>
                            
                            <div class="sdk-item">
                                <div class="sdk-icon"><i class="material-icons">coffee</i></div>
                                <div class="sdk-info">
                                    <h3>Java</h3>
                                    <p>Our Java SDK is compatible with Java 8+ and includes Spring Boot integration.</p>
                                    <a href="#" class="btn-sdk">View on GitHub</a>
                                </div>
                            </div>
                        </div>
                    </div>

                    <div class="content-section" id="examples">
                        <h2>Code Examples</h2>
                        
                        <div class="code-tabs">
                            <div class="tab-buttons">
                                <button class="tab-btn active" data-tab="php">PHP</button>
                                <button class="tab-btn" data-tab="javascript">JavaScript</button>
                                <button class="tab-btn" data-tab="python">Python</button>
                                <button class="tab-btn" data-tab="java">Java</button>
                            </div>
                            
                            <div class="tab-content">
                                <div class="tab-pane active" id="php-tab">
                                    <h3>Recording Attendance in PHP</h3>
                                    <div class="code-block">
                                        <pre><code class="language-php">
require 'vendor/autoload';

use BthDevelopers\BiometricApi\Client;

// Initialize the client with your API key
$client = new Client('YOUR_API_KEY');

try {
    // Record a check-in
    $attendance = $client->attendance->create([
        'user_id' => 'usr_123456789',
        'device_id' => 'dev_123456789',
        'type' => 'check_in',
        'timestamp' => date('c') // Current time in ISO 8601 format
    ]);
    
    echo "Attendance recorded successfully!\n";
    echo "Attendance ID: " . $attendance->id . "\n";
    echo "User ID: " . $attendance->user_id . "\n";
    echo "Type: " . $attendance->type . "\n";
    echo "Timestamp: " . $attendance->timestamp . "\n";
} catch (\Exception $e) {
    echo "Error: " . $e->getMessage() . "\n";
}
</code></pre>
                                    </div>
                                </div>
                                
                                <div class="tab-pane" id="javascript-tab">
                                    <h3>Recording Attendance in JavaScript</h3>
                                    <div class="code-block">
                                        <pre><code class="language-javascript">// Install the SDK: npm install @bthdevelopers/biometric-api-js

const { BiometricApiClient } = require('@bthdevelopers/biometric-api-js');

// Initialize the client with your API key
const client = new BiometricApiClient('YOUR_API_KEY');

// Record a check-in
async function recordAttendance() {
  try {
    const attendance = await client.attendance.create({
      user_id: 'usr_123456789',
      device_id: 'dev_123456789',
      type: 'check_in',
      timestamp: new Date().toISOString()
    });
    
    console.log('Attendance recorded successfully!');
    console.log('Attendance ID:', attendance.id);
    console.log('User ID:', attendance.user_id);
    console.log('Type:', attendance.type);
    console.log('Timestamp:', attendance.timestamp);
  } catch (error) {
    console.error('Error:', error.message);
  }
}

recordAttendance();</code></pre>
                                    </div>
                                </div>
                                
                                <div class="tab-pane" id="python-tab">
                                    <h3>Recording Attendance in Python</h3>
                                    <div class="code-block">
                                        <pre><code class="language-python"># Install the SDK: pip install bthdevelopers-biometric-api

from bthdevelopers.biometric_api import BiometricApiClient
from datetime import datetime
import iso8601

# Initialize the client with your API key
client = BiometricApiClient('YOUR_API_KEY')

try:
    # Record a check-in
    attendance = client.attendance.create(
        user_id='usr_123456789',
        device_id='dev_123456789',
        type='check_in',
        timestamp=datetime.now().isoformat()
    )
    
    print('Attendance recorded successfully!')
    print(f'Attendance ID: {attendance.id}')
    print(f'User ID: {attendance.user_id}')
    print(f'Type: {attendance.type}')
    print(f'Timestamp: {attendance.timestamp}')
except Exception as e:
    print(f'Error: {str(e)}')</code></pre>
                                    </div>
                                </div>
                                
                                <div class="tab-pane" id="java-tab">
                                    <h3>Recording Attendance in Java</h3>
                                    <div class="code-block">
                                        <pre><code class="language-java">// Add the dependency to your build.gradle:
// implementation 'com.bthdevelopers:biometric-api-java:1.0.0'

import com.bthdevelopers.biometricapi.BiometricApiClient;
import com.bthdevelopers.biometricapi.models.Attendance;
import com.bthdevelopers.biometricapi.requests.AttendanceCreateRequest;

import java.time.Instant;

public class RecordAttendanceExample {
    public static void main(String[] args) {
        // Initialize the client with your API key
        BiometricApiClient client = new BiometricApiClient("YOUR_API_KEY");
        
        try {
            // Record a check-in
            AttendanceCreateRequest request = new AttendanceCreateRequest.Builder()
                .userId("usr_123456789")
                .deviceId("dev_123456789")
                .type("check_in")
                .timestamp(Instant.now().toString())
                .build();
                
            Attendance attendance = client.attendance().create(request);
            
            System.out.println("Attendance recorded successfully!");
            System.out.println("Attendance ID: " + attendance.getId());
            System.out.println("User ID: " + attendance.getUserId());
            System.out.println("Type: " + attendance.getType());
            System.out.println("Timestamp: " + attendance.getTimestamp());
        } catch (Exception e) {
            System.err.println("Error: " + e.getMessage());
        }
    }
}</code></pre>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>

                    <div class="content-section" id="changelog">
                        <h2>Changelog</h2>
                        
                        <div class="changelog-item">
                            <div class="version">v1.2.0</div>
                            <div class="date">2023-03-15</div>
                            <div class="changes">
                                <h3>Added</h3>
                                <ul>
                                    <li>New endpoint for bulk user import</li>
                                    <li>Support for facial recognition devices</li>
                                    <li>PDF export for reports</li>
                                </ul>
                                <h3>Fixed</h3>
                                <ul>
                                    <li>Issue with timezone handling in attendance records</li>
                                    <li>Performance improvements for large data sets</li>
                                </ul>
                            </div>
                        </div>
                        
                        <div class="changelog-item">
                            <div class="version">v1.1.0</div>
                            <div class="date">2023-02-01</div>
                            <div class="changes">
                                <h3>Added</h3>
                                <ul>
                                    <li>Webhooks for real-time notifications</li>
                                    <li>New report types: monthly and custom date range</li>
                                </ul>
                                <h3>Changed</h3>
                                <ul>
                                    <li>Improved error messages for better debugging</li>
                                    <li>Updated rate limits for all plans</li>
                                </ul>
                            </div>
                        </div>
                        
                        <div class="changelog-item">
                            <div class="version">v1.0.0</div>
                            <div class="date">2023-01-01</div>
                            <div class="changes">
                                <h3>Initial Release</h3>
                                <ul>
                                    <li>Basic CRUD operations for users, devices, and attendance</li>
                                    <li>Authentication with API keys</li>
                                    <li>Basic reporting functionality</li>
                                </ul>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </section>

    <!-- CTA Section -->
    <section class="cta-section">
        <div class="container">
            <div class="cta-content">
                <h2>Ready to Integrate Our <span class="dynamic-gradient-text">Biometric API</span>?</h2>
                <p>Contact us today to get started with our Biometric API and transform your attendance management system.</p>
                <a href="contact" class="btn btn-primary">Get in Touch</a>
            </div>
        </div>
    </section>

    <?php include 'footer.php'; ?>

    <!-- Scripts -->
    <script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.7.0/highlight.min.js"></script>
    <script src="js/main.js?v=<?php echo time(); ?>"></script>
    <script src="js/api-docs.js?v=<?php echo time(); ?>"></script>
</body>
</html>
