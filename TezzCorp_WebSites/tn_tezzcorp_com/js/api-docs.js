// Wait for DOM to load
document.addEventListener('DOMContentLoaded', function() {
    // Initialize syntax highlighting
    hljs.highlightAll();
    
    // Sidebar navigation
    const sidebarLinks = document.querySelectorAll('.sidebar-menu a');
    const contentSections = document.querySelectorAll('.content-section');
    
    // Set active link based on scroll position
    function setActiveLink() {
        let currentSection = '';
        
        contentSections.forEach(section => {
            const sectionTop = section.offsetTop - 100;
            const sectionHeight = section.offsetHeight;
            const scrollPosition = window.scrollY;
            
            if (scrollPosition >= sectionTop && scrollPosition < sectionTop + sectionHeight) {
                currentSection = section.getAttribute('id');
            }
        });
        
        sidebarLinks.forEach(link => {
            link.classList.remove('active');
            if (link.getAttribute('href') === '#' + currentSection) {
                link.classList.add('active');
                
                // If it's a submenu item, also highlight parent
                const parentLi = link.closest('li').parentElement;
                if (parentLi.classList.contains('submenu')) {
                    parentLi.previousElementSibling.classList.add('active');
                }
            }
        });
    }
    
    // Smooth scroll to section when clicking on sidebar links
    sidebarLinks.forEach(link => {
        link.addEventListener('click', function(e) {
            e.preventDefault();
            
            const targetId = this.getAttribute('href').substring(1);
            const targetSection = document.getElementById(targetId);
            
            if (targetSection) {
                window.scrollTo({
                    top: targetSection.offsetTop - 80,
                    behavior: 'smooth'
                });
                
                // Update URL hash
                history.pushState(null, null, '#' + targetId);
                
                // Set active link
                sidebarLinks.forEach(link => link.classList.remove('active'));
                this.classList.add('active');
                
                // If it's a submenu item, also highlight parent
                const parentLi = this.closest('li').parentElement;
                if (parentLi.classList.contains('submenu')) {
                    parentLi.previousElementSibling.classList.add('active');
                }
            }
        });
    });
    
    // Check for hash in URL and scroll to section
    if (window.location.hash) {
        const targetId = window.location.hash.substring(1);
        const targetSection = document.getElementById(targetId);
        
        if (targetSection) {
            setTimeout(() => {
                window.scrollTo({
                    top: targetSection.offsetTop - 80,
                    behavior: 'smooth'
                });
                
                // Set active link
                sidebarLinks.forEach(link => {
                    if (link.getAttribute('href') === '#' + targetId) {
                        link.classList.add('active');
                        
                        // If it's a submenu item, also highlight parent
                        const parentLi = link.closest('li').parentElement;
                        if (parentLi.classList.contains('submenu')) {
                            parentLi.previousElementSibling.classList.add('active');
                        }
                    }
                });
            }, 100);
        }
    }
    
    // Update active link on scroll
    window.addEventListener('scroll', setActiveLink);
    
    // Search functionality
    const searchInput = document.getElementById('docs-search');
    
    searchInput.addEventListener('input', function() {
        const searchTerm = this.value.toLowerCase();
        
        if (searchTerm.length < 2) {
            // Reset all sections and links
            contentSections.forEach(section => {
                section.style.display = 'block';
            });
            
            sidebarLinks.forEach(link => {
                link.style.display = 'block';
            });
            
            return;
        }
        
        // Hide all sections first
        contentSections.forEach(section => {
            section.style.display = 'none';
        });
        
        // Show sections that match the search term
        contentSections.forEach(section => {
            const sectionText = section.textContent.toLowerCase();
            if (sectionText.includes(searchTerm)) {
                section.style.display = 'block';
                
                // Also show the corresponding sidebar link
                const sectionId = section.getAttribute('id');
                sidebarLinks.forEach(link => {
                    if (link.getAttribute('href') === '#' + sectionId) {
                        link.style.display = 'block';
                        
                        // If it's a submenu item, also show parent
                        const parentLi = link.closest('li').parentElement;
                        if (parentLi.classList.contains('submenu')) {
                            parentLi.previousElementSibling.style.display = 'block';
                        }
                    }
                });
            }
        });
    });
    
    // Code tabs
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabPanes = document.querySelectorAll('.tab-pane');
    
    tabButtons.forEach(button => {
        button.addEventListener('click', function() {
            const tabId = this.getAttribute('data-tab');
            
            // Hide all tab panes
            tabPanes.forEach(pane => {
                pane.classList.remove('active');
            });
            
            // Show the selected tab pane
            document.getElementById(tabId + '-tab').classList.add('active');
            
            // Update active button
            tabButtons.forEach(btn => {
                btn.classList.remove('active');
            });
            
            this.classList.add('active');
        });
    });
    
    // Copy code to clipboard
    const codeBlocks = document.querySelectorAll('.code-block');
    
    codeBlocks.forEach(block => {
        // Create copy button
        const copyButton = document.createElement('button');
        copyButton.className = 'copy-btn';
        copyButton.innerHTML = '<i class="fas fa-copy"></i>';
        copyButton.title = 'Copy to clipboard';
        
        // Add copy button to code block
        block.appendChild(copyButton);
        
        // Add click event to copy button
        copyButton.addEventListener('click', function() {
            const code = block.querySelector('code').textContent;
            
            // Copy to clipboard
            navigator.clipboard.writeText(code).then(() => {
                // Show success message
                this.innerHTML = '<i class="fas fa-check"></i>';
                this.classList.add('copied');
                
                // Reset after 2 seconds
                setTimeout(() => {
                    this.innerHTML = '<i class="fas fa-copy"></i>';
                    this.classList.remove('copied');
                }, 2000);
            }).catch(err => {
                console.error('Failed to copy: ', err);
            });
        });
    });
    
    // Add styles for copy button
    const style = document.createElement('style');
    style.textContent = `
        .code-block {
            position: relative;
        }
        
        .copy-btn {
            position: absolute;
            top: 5px;
            right: 5px;
            width: 30px;
            height: 30px;
            background-color: rgba(255, 255, 255, 0.1);
            border: none;
            border-radius: 4px;
            color: #fff;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            opacity: 0;
            transition: opacity 0.3s ease;
        }
        
        .code-block:hover .copy-btn {
            opacity: 1;
        }
        
        .copy-btn.copied {
            background-color: #4CAF50;
        }
    `;
    
    document.head.appendChild(style);
});