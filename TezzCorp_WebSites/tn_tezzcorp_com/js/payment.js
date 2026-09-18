// Payment Page JavaScript
document.addEventListener('DOMContentLoaded', function() {
    // DOM Elements
    const methodOptions = document.querySelectorAll('.method-option');
    const methodForms = document.querySelectorAll('.method-form');
    
    // Payment method selection
    methodOptions.forEach(option => {
        option.addEventListener('click', function() {
            const method = this.getAttribute('data-method');
            
            // Update active method option
            methodOptions.forEach(opt => opt.classList.remove('active'));
            this.classList.add('active');
            
            // Show corresponding form
            methodForms.forEach(form => form.classList.remove('active'));
            document.getElementById(`${method}-form`).classList.add('active');
        });
    });
    
    // Card form validation
    const cardForm = document.getElementById('card-form');
    if (cardForm) {
        const cardNumber = document.getElementById('card-number');
        const cardExpiry = document.getElementById('card-expiry');
        const cardCvv = document.getElementById('card-cvv');
        
        // Format card number
        if (cardNumber) {
            cardNumber.addEventListener('input', function(e) {
                let value = e.target.value.replace(/\D/g, '');
                if (value.length > 16) {
                    value = value.slice(0, 16);
                }
                
                // Add spaces every 4 digits
                const formattedValue = value.replace(/(\d{4})(?=\d)/g, '$1 ');
                e.target.value = formattedValue;
            });
        }
        
        // Format expiry date
        if (cardExpiry) {
            cardExpiry.addEventListener('input', function(e) {
                let value = e.target.value.replace(/\D/g, '');
                if (value.length > 4) {
                    value = value.slice(0, 4);
                }
                
                if (value.length > 2) {
                    value = value.slice(0, 2) + '/' + value.slice(2);
                }
                
                e.target.value = value;
            });
        }
        
        // Format CVV
        if (cardCvv) {
            cardCvv.addEventListener('input', function(e) {
                let value = e.target.value.replace(/\D/g, '');
                if (value.length > 3) {
                    value = value.slice(0, 3);
                }
                
                e.target.value = value;
            });
        }
        
        // Form submission
        cardForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            // Validate card number
            const cardNumberValue = cardNumber.value.replace(/\s/g, '');
            if (cardNumberValue.length !== 16) {
                alert('Please enter a valid 16-digit card number');
                return;
            }
            
            // Validate expiry date
            const expiryValue = cardExpiry.value;
            if (!expiryValue.match(/^\d{2}\/\d{2}$/)) {
                alert('Please enter a valid expiry date (MM/YY)');
                return;
            }
            
            // Validate CVV
            const cvvValue = cardCvv.value;
            if (cvvValue.length !== 3) {
                alert('Please enter a valid 3-digit CVV');
                return;
            }
            
            // Submit form
            processPayment('card');
        });
    }
    
    // PhonePe form submission
    const phonepeForm = document.getElementById('phonepe-form');
    if (phonepeForm) {
        phonepeForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            // Validate phone number
            const phoneNumber = document.getElementById('phonepe-number').value;
            if (!phoneNumber.match(/^\d{10}$/)) {
                alert('Please enter a valid 10-digit phone number');
                return;
            }
            
            // Submit form
            processPayment('phonepe');
        });
    }
    
    // Google Pay form submission
    const googlepayForm = document.getElementById('googlepay-form');
    if (googlepayForm) {
        googlepayForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            // Validate UPI ID
            const upiId = document.getElementById('googlepay-number').value;
            if (!upiId.includes('@')) {
                alert('Please enter a valid UPI ID');
                return;
            }
            
            // Submit form
            processPayment('googlepay');
        });
    }
    
    // Net Banking form submission
    const netbankingForm = document.getElementById('netbanking-form');
    if (netbankingForm) {
        netbankingForm.addEventListener('submit', function(e) {
            e.preventDefault();
            
            // Validate bank selection
            const bank = document.getElementById('bank-select').value;
            if (!bank) {
                alert('Please select a bank');
                return;
            }
            
            // Submit form
            processPayment('netbanking');
        });
    }
    
    // Process payment
    function processPayment(method) {
        // Show loading state
        document.querySelector(`#${method}-form button`).innerHTML = '<i class="fas fa-spinner fa-spin"></i> Processing...';
        document.querySelector(`#${method}-form button`).disabled = true;
        
        // In a real application, this would submit the form to the server
        // For demo purposes, we'll simulate a successful payment
        
        setTimeout(() => {
            // Redirect to success page
            window.location.href = 'payment-success.html';
        }, 2000);
    }
});