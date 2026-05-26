const fs = require('fs').promises;
const path = require('path');
const { JSDOM } = require('jsdom');
const htmlValidator = require('html-validator');

class HTMLQualityChecker {
    constructor(options = {}) {
        this.options = {
            validateHtml: true,
            checkWcag: true,
            wcagLevel: 'AA',
            verbose: false,
            ...options
        };
    }

    /**
     * Check HTML file
     * @param {string} filePath - path to HTML file
     */
    async checkFile(filePath) {
        try {
            const html = await fs.readFile(filePath, 'utf-8');
            return await this.checkHtml(html, filePath);
        } catch (error) {
            return {
                valid: false,
                errors: [`File read error: ${error.message}`],
                warnings: []
            };
        }
    }

    /**
     * Check HTML string
     * @param {string} html - HTML code
     * @param {string} source - source identifier
     */
    async checkHtml(html, source = 'string') {
        const result = {
            source,
            valid: true,
            errors: [],
            warnings: [],
            wcag: []
        };

        if (this.options.verbose) {
            console.log(`[INFO] Starting validation for: ${source}`);
        }

        // 1. HTML syntax validation
        if (this.options.validateHtml) {
            if (this.options.verbose) console.log('[INFO] Running HTML syntax validation...');
            const validationResult = await this.validateHtmlSyntax(html);
            if (!validationResult.valid) {
                result.valid = false;
                result.errors.push(...validationResult.errors);
            }
            result.warnings.push(...validationResult.warnings);
        }

        // 2. WCAG 2.1 compliance check
        if (this.options.checkWcag) {
            if (this.options.verbose) console.log('[INFO] Running WCAG 2.1 compliance check...');
            const wcagResult = await this.checkWcagCompliance(html);
            result.wcag = wcagResult;
            
            const criticalIssues = wcagResult.filter(issue => issue.type === 'error');
            if (criticalIssues.length > 0) {
                result.valid = false;
            }
        }

        if (this.options.verbose) console.log('[INFO] Validation completed');
        return result;
    }

    /**
     * HTML syntax validation
     */
    async validateHtmlSyntax(html) {
        const result = {
            valid: true,
            errors: [],
            warnings: []
        };

        try {
            // Using html-validator (W3C validator API)
            const validationResult = await htmlValidator({
                data: html,
                format: 'json'
            });

            if (validationResult.messages) {
                validationResult.messages.forEach(msg => {
                    const issue = {
                        line: msg.lastLine || msg.line,
                        column: msg.lastColumn || msg.column,
                        message: msg.message,
                        code: msg.code
                    };

                    if (msg.type === 'error') {
                        result.errors.push(issue);
                        result.valid = false;
                    } else if (msg.type === 'warning' || msg.type === 'info') {
                        result.warnings.push(issue);
                    }
                });
            }

            // Additional JSDOM checks
            await this.checkWithJSDOM(html, result);

        } catch (error) {
            console.error('[ERROR] Validation failed:', error);
            result.errors.push({
                message: `Validation error: ${error.message}`
            });
            result.valid = false;
        }

        return result;
    }

    /**
     * JSDOM basic syntax checks
     */
    async checkWithJSDOM(html, result) {
        try {
            const dom = new JSDOM(html);
            const document = dom.window.document;

            // Check for empty elements
            const emptyElements = document.querySelectorAll('p:empty, div:empty, span:empty');
            emptyElements.forEach(el => {
                result.warnings.push({
                    message: `Empty element: <${el.tagName.toLowerCase()}>`,
                    type: 'warning'
                });
            });

            // Check for missing alt in images
            const imagesWithoutAlt = document.querySelectorAll('img:not([alt])');
            imagesWithoutAlt.forEach(img => {
                result.warnings.push({
                    message: 'Image without alt attribute',
                    type: 'warning'
                });
            });

            // Check for missing lang attribute in html tag
            const htmlTag = document.querySelector('html');
            if (htmlTag && !htmlTag.hasAttribute('lang')) {
                result.warnings.push({
                    message: 'Missing lang attribute in <html> tag',
                    type: 'warning'
                });
            }

            // Check for missing title
            const title = document.querySelector('title');
            if (!title) {
                result.warnings.push({
                    message: 'Missing <title> tag',
                    type: 'warning'
                });
            }

            // Check for meta description
            const metaDesc = document.querySelector('meta[name="description"]');
            if (!metaDesc) {
                result.warnings.push({
                    message: 'Missing meta description',
                    type: 'info'
                });
            }

        } catch (error) {
            result.errors.push({
                message: `DOM parsing error: ${error.message}`,
                type: 'error'
            });
        }
    }

    /**
     * WCAG 2.1 compliance check
     */
    async checkWcagCompliance(html) {
        const issues = [];
        const dom = new JSDOM(html);
        const document = dom.window.document;

        // 1.1.1 Non-text Content (Level A)
        const images = document.querySelectorAll('img');
        images.forEach((img, index) => {
            if (!img.hasAttribute('alt')) {
                issues.push({
                    criterion: '1.1.1',
                    level: 'A',
                    type: 'error',
                    description: 'Image missing alt text',
                    element: img.outerHTML.substring(0, 100)
                });
            } else if (img.getAttribute('alt') === '') {
                issues.push({
                    criterion: '1.1.1',
                    level: 'A',
                    type: 'info',
                    description: 'Decorative image with empty alt',
                    element: img.outerHTML.substring(0, 100)
                });
            }
        });

        // 1.3.1 Info and Relationships (Level A)
        const headings = document.querySelectorAll('h1, h2, h3, h4, h5, h6');
        const headingLevels = Array.from(headings).map(h => parseInt(h.tagName[1]));
        
        // Check for skipped heading levels
        for (let i = 1; i < headingLevels.length; i++) {
            if (headingLevels[i] - headingLevels[i-1] > 1) {
                issues.push({
                    criterion: '1.3.1',
                    level: 'A',
                    type: 'warning',
                    description: `Skipped heading level: from h${headingLevels[i-1]} to h${headingLevels[i]}`,
                    element: headings[i].outerHTML.substring(0, 100)
                });
            }
        }

        // Check for multiple h1
        const h1Count = document.querySelectorAll('h1').length;
        if (h1Count > 1) {
            issues.push({
                criterion: '1.3.1',
                level: 'A',
                type: 'warning',
                description: `Multiple h1 headings found (${h1Count})`,
                element: 'document'
            });
        }

        // 1.4.3 Contrast (Level AA) - basic check
        const textElements = document.querySelectorAll('p, span, div, h1, h2, h3, h4, h5, h6, a, button');
        textElements.forEach(el => {
            const style = dom.window.getComputedStyle(el);
            const color = style.color;
            const backgroundColor = style.backgroundColor;
            const fontSize = parseFloat(style.fontSize);

            // Check for white text on white background (simplified)
            if (backgroundColor === 'rgba(0, 0, 0, 0)' || backgroundColor === 'transparent') {
                if (color === 'rgb(255, 255, 255)' || color === 'white') {
                    issues.push({
                        criterion: '1.4.3',
                        level: 'AA',
                        type: 'error',
                        description: 'White text on white background (potential low contrast)',
                        element: el.outerHTML.substring(0, 100)
                    });
                }
            }

            // Check for very small text
            if (fontSize < 12) {
                issues.push({
                    criterion: '1.4.3',
                    level: 'AA',
                    type: 'warning',
                    description: `Very small text (${fontSize}px) may affect readability`,
                    element: el.outerHTML.substring(0, 100)
                });
            }
        });

        // 2.4.4 Link Purpose (Level A)
        const links = document.querySelectorAll('a');
        links.forEach(link => {
            if (link.textContent.trim() === '' && !link.querySelector('img')) {
                issues.push({
                    criterion: '2.4.4',
                    level: 'A',
                    type: 'error',
                    description: 'Empty link without text content',
                    element: link.outerHTML.substring(0, 100)
                });
            }

            // Check for generic link text
            const linkText = link.textContent.trim().toLowerCase();
            if (['click here', 'read more', 'more', 'link'].includes(linkText)) {
                issues.push({
                    criterion: '2.4.4',
                    level: 'A',
                    type: 'warning',
                    description: `Generic link text: "${linkText}"`,
                    element: link.outerHTML.substring(0, 100)
                });
            }
        });

        // 2.4.6 Headings and Labels (Level AA)
        const inputs = document.querySelectorAll('input:not([type="hidden"]), select, textarea');
        inputs.forEach(input => {
            const id = input.id;
            if (id) {
                const label = document.querySelector(`label[for="${id}"]`);
                if (!label && !input.hasAttribute('aria-label') && !input.hasAttribute('aria-labelledby')) {
                    issues.push({
                        criterion: '2.4.6',
                        level: 'AA',
                        type: 'warning',
                        description: 'Form input without associated label',
                        element: input.outerHTML.substring(0, 100)
                    });
                }
            } else if (!input.hasAttribute('aria-label') && !input.hasAttribute('aria-labelledby')) {
                issues.push({
                    criterion: '2.4.6',
                    level: 'AA',
                    type: 'warning',
                    description: 'Form input without id or ARIA label',
                    element: input.outerHTML.substring(0, 100)
                });
            }
        });

        // 2.4.7 Focus Visible (Level AA)
        const focusableElements = document.querySelectorAll('a, button, input, select, textarea, [tabindex]:not([tabindex="-1"])');
        focusableElements.forEach(el => {
            const style = dom.window.getComputedStyle(el);
            const outline = style.outline;
            
            if (outline === '0px none rgb(0, 0, 0)' || outline === 'none') {
                issues.push({
                    criterion: '2.4.7',
                    level: 'AA',
                    type: 'warning',
                    description: 'Element may lack visible focus indicator',
                    element: el.outerHTML.substring(0, 100)
                });
            }
        });

        // 3.2.2 On Input (Level A)
        const selects = document.querySelectorAll('select');
        selects.forEach(select => {
            if (!select.hasAttribute('onchange') && !select.getAttribute('onchange')?.includes('this.form.submit()')) {
                // This is a simplified check - actual implementation would need more context
                issues.push({
                    criterion: '3.2.2',
                    level: 'A',
                    type: 'info',
                    description: 'Select element without explicit form submission control',
                    element: select.outerHTML.substring(0, 100)
                });
            }
        });

        // 3.3.2 Labels or Instructions (Level A)
        const forms = document.querySelectorAll('form');
        forms.forEach(form => {
            const hasInstructions = form.querySelector('p, span, legend, div[role="note"]');
            if (!hasInstructions) {
                issues.push({
                    criterion: '3.3.2',
                    level: 'A',
                    type: 'info',
                    description: 'Form without explicit instructions',
                    element: form.outerHTML.substring(0, 100)
                });
            }
        });

        // 4.1.1 Parsing (Level A)
        // Check for duplicate ids
        const ids = {};
        const elementsWithId = document.querySelectorAll('[id]');
        elementsWithId.forEach(el => {
            const id = el.id;
            if (ids[id]) {
                issues.push({
                    criterion: '4.1.1',
                    level: 'A',
                    type: 'error',
                    description: `Duplicate ID: ${id}`,
                    element: el.outerHTML.substring(0, 100)
                });
            } else {
                ids[id] = true;
            }
        });

        // Check for ARIA roles
        const elementsWithAria = document.querySelectorAll('[role]');
        elementsWithAria.forEach(el => {
            const role = el.getAttribute('role');
            const validRoles = ['button', 'link', 'checkbox', 'radio', 'tab', 'menu', 'dialog'];
            if (!validRoles.includes(role)) {
                issues.push({
                    criterion: '4.1.2',
                    level: 'A',
                    type: 'warning',
                    description: `Potentially invalid ARIA role: "${role}"`,
                    element: el.outerHTML.substring(0, 100)
                });
            }
        });

        return issues;
    }

    /**
     * Format result for output
     */
    formatResult(result) {
        let output = `\nHTML QUALITY CHECK REPORT\n`;
        output += `Source: ${result.source}\n`;
        output += `=${'='.repeat(50)}\n\n`;

        // Summary
        output += `SUMMARY\n`;
        output += `Status: ${result.valid ? 'PASSED' : 'FAILED'}\n`;
        output += `Errors: ${result.errors.length}\n`;
        output += `Warnings: ${result.warnings.length}\n`;
        output += `WCAG Issues: ${result.wcag.length}\n\n`;

        // Errors
        if (result.errors.length > 0) {
            output += `ERRORS (${result.errors.length}):\n`;
            result.errors.forEach((err, i) => {
                if (typeof err === 'string') {
                    output += `  ${i+1}. ${err}\n`;
                } else {
                    const location = err.line ? `[Line ${err.line}] ` : '';
                    output += `  ${i+1}. ${location}${err.message}\n`;
                }
            });
            output += '\n';
        }

        // Warnings
        if (result.warnings.length > 0) {
            output += `WARNINGS (${result.warnings.length}):\n`;
            result.warnings.forEach((warn, i) => {
                if (typeof warn === 'string') {
                    output += `  ${i+1}. ${warn}\n`;
                } else {
                    output += `  ${i+1}. ${warn.message}\n`;
                }
            });
            output += '\n';
        }

        // WCAG Issues
        if (result.wcag.length > 0) {
            output += `WCAG 2.1 ISSUES (${result.wcag.length}):\n`;
            
            // Group by level
            const levelA = result.wcag.filter(i => i.level === 'A');
            const levelAA = result.wcag.filter(i => i.level === 'AA');
            const levelAAA = result.wcag.filter(i => i.level === 'AAA');

            if (levelA.length > 0) {
                output += `\n  Level A (${levelA.length}):\n`;
                levelA.forEach((issue, i) => {
                    const type = issue.type.toUpperCase();
                    output += `    ${i+1}. [${type}] ${issue.criterion}: ${issue.description}\n`;
                });
            }

            if (levelAA.length > 0) {
                output += `\n  Level AA (${levelAA.length}):\n`;
                levelAA.forEach((issue, i) => {
                    const type = issue.type.toUpperCase();
                    output += `    ${i+1}. [${type}] ${issue.criterion}: ${issue.description}\n`;
                });
            }

            if (levelAAA.length > 0) {
                output += `\n  Level AAA (${levelAAA.length}):\n`;
                levelAAA.forEach((issue, i) => {
                    const type = issue.type.toUpperCase();
                    output += `    ${i+1}. [${type}] ${issue.criterion}: ${issue.description}\n`;
                });
            }
            output += '\n';
        }

        // Recommendations
        if (!result.valid) {
            output += `RECOMMENDATIONS:\n`;
            output += `  1. Fix all ERROR level issues\n`;
            output += `  2. Review WARNING level issues\n`;
            output += `  3. Run the check again after fixes\n`;
        } else {
            output += `✓ All checks passed\n`;
        }

        return output;
    }

    /**
     * Get JSON result
     */
    getJSON(result) {
        return JSON.stringify(result, null, 2);
    }
}

// CLI usage example
async function main() {
    const checker = new HTMLQualityChecker({
        validateHtml: true,
        checkWcag: true,
        wcagLevel: 'AA',
        verbose: true
    });

    // Example HTML with various issues
    const htmlString = `
    <!DOCTYPE html>
    <html>
    <head>
        <title>Test Page</title>
    </head>
    <body>
        <h1>Main Title</h1>
        <h3>Skipped h2</h3>
        
        <img src="image.jpg">
        
        <a href="#">Click here</a>
        
        <form>
            <input type="text">
            <button>Submit</button>
        </form>
        
        <div id="duplicate">Content</div>
        <div id="duplicate">Duplicate ID</div>
    </body>
    </html>
    `;

    console.log('[INFO] Starting HTML quality check...');
    const result = await checker.checkHtml(htmlString, 'example.html');
    
    // Text report
    console.log(checker.formatResult(result));
    
    // JSON output (optional)
    if (process.argv.includes('--json')) {
        console.log('\nJSON OUTPUT:');
        console.log(checker.getJSON(result));
    }
}

// Run if called directly
if (require.main === module) {
    main().catch(error => {
        console.error('[FATAL]', error);
        process.exit(1);
    });
}

module.exports = HTMLQualityChecker;
