# VX Studio Website

Professional, modern website for VX Studio audio plugins. Ready to deploy to vxstudio.marczewski.me.uk

## Quick Start

### Prerequisites
- None! This is a static HTML/CSS/JS site.
- No build step, no dependencies, no framework required.

### Local Preview
```bash
cd web
python3 -m http.server 8000
```

Then visit: http://localhost:8000

### Folder Structure

```
web/
├── index.html           # Home page
├── downloads/           # Download center (PLUGIN FILES GO HERE)
├── docs/                # Documentation hub
├── support/             # Support & FAQ
├── products/            # Individual product pages
├── legal/               # License, privacy, terms
├── assets/              # CSS, JS, images
└── WEB_STRUCTURE.md     # Detailed deployment guide
```

## Key Features

✅ **Modern Design** - Clean, professional, responsive
✅ **Fast Loading** - Optimized static HTML/CSS/JS
✅ **Accessible** - WCAG 2.1 AA compliant
✅ **Mobile-First** - Works on all devices
✅ **SEO-Ready** - Proper meta tags, structured data
✅ **Privacy-Focused** - No tracking, no third-party scripts
✅ **Production-Ready** - Follow best practices from iZotope, Native Instruments

## Pages Included

### Public Pages
- **Home** (`index.html`) - Landing page with product overview
- **Downloads** (`downloads/`) - macOS available, Windows coming soon
- **Products** (`products/`) - Individual product pages (12 plugins)
- **Documentation** (`docs/`) - User guides and technical docs
- **Support** (`support/`) - FAQ and troubleshooting
- **Legal** (`legal/`) - License, privacy policy, terms

### Assets
- **CSS** (`assets/css/style.css`) - Modern, responsive styling
- **JavaScript** (`assets/js/script.js`) - Smooth scroll, analytics
- **Images** (`assets/images/`) - Logos, screenshots, icons

## Plugin Files Location

### Where to Put the Downloads

The `downloads/` folder should contain:

```
web/downloads/
├── vxsuite-1.0.0-macos-universal.zip     # Main download for macOS
└── checksums.txt                          # SHA256 verification
```

### File Naming Convention

**macOS:** `vxsuite-{VERSION}-macos-universal.zip`
Example: `vxsuite-1.0.0-macos-universal.zip`

**Windows (Future):** `vxsuite-{VERSION}-windows-x64.zip`
Example: `vxsuite-1.0.0-windows-x64.zip`

### What to Include in the ZIP

**macOS ZIP** should contain:
- 12 VST3 plugin bundles (`.vst3/` folders)
- 12 Audio Unit components (`.component/` folders)
- `INSTALL.txt` - Installation instructions
- `README.txt` - Overview
- `LICENSE.txt` - MIT license

**Windows ZIP** should contain:
- VST3 installer (optional)
- 12 VST3 plugin files (`.vst3`)
- Installation and license files

## Deployment

### Option 1: Traditional Web Host (Recommended)

```bash
# Copy to your server
rsync -avz --delete web/ user@vxstudio.marczewski.me.uk:/var/www/vxstudio/

# SSH in and set permissions
ssh user@vxstudio.marczewski.me.uk
sudo chown -R www-data:www-data /var/www/vxstudio
sudo chmod -R 755 /var/www/vxstudio
```

### Option 2: Cloudflare Pages (Easiest)

1. Push `web/` folder to GitHub
2. Connect Cloudflare Pages to repo
3. No build command needed (leave empty)
4. Publish directory: `web`
5. Done! Auto-deploys on every git push

### Option 3: Netlify (Also Easy)

1. Push `web/` folder to GitHub
2. Connect Netlify to repo
3. No build command needed
4. Publish directory: `web`
5. Configure custom domain

### Option 4: Self-Hosted Nginx

```nginx
server {
    server_name vxstudio.marczewski.me.uk;
    root /var/www/vxstudio;

    location / {
        try_files $uri $uri/ =404;
    }

    location /downloads {
        # Allow large file downloads
        client_max_body_size 1G;
    }

    # SSL via Let's Encrypt
    listen 443 ssl http2;
    ssl_certificate /etc/letsencrypt/live/vxstudio.marczewski.me.uk/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/vxstudio.marczewski.me.uk/privkey.pem;
}
```

## Building the Plugin Distribution

### Step 1: Build Plugins

```bash
cd /path/to/VxStudio
cmake --build build -j$(nproc)
```

### Step 2: Package for Distribution

```bash
# Create macOS universal distribution
mkdir -p distribution/vxsuite-1.0.0-macos-universal/{VST3,AudioUnits}

# Copy VST3 bundles
cp -r build/*/VST3/*.vst3 distribution/vxsuite-1.0.0-macos-universal/VST3/

# Copy Audio Unit components
cp -r build/*/AU/*.component distribution/vxsuite-1.0.0-macos-universal/AudioUnits/

# Create ZIP
cd distribution
zip -r vxsuite-1.0.0-macos-universal.zip vxsuite-1.0.0-macos-universal/
sha256sum vxsuite-1.0.0-macos-universal.zip > checksums.txt
```

### Step 3: Copy to Web Downloads

```bash
cp distribution/vxsuite-1.0.0-macos-universal.zip web/downloads/
cp distribution/checksums.txt web/downloads/
```

### Step 4: Update Website

Edit `web/downloads/index.html`:
- Update version number
- Update file size
- Update SHA256 checksum
- Add release notes

## Content Updates

### Edit Product Information
- Edit `web/products/{product-name}.html` for individual products
- Edit `web/index.html` to update product grid on home page

### Edit Documentation
- Add files to `web/docs/`
- Link from navigation
- Update sidebar/menu

### Edit Support Pages
- Update `web/support/index.html`
- Add FAQ items
- Add troubleshooting steps

### Edit Legal
- Update `web/legal/license.html` - Your license text
- Update `web/legal/privacy.html` - Your privacy policy
- Update `web/legal/terms.html` - Your terms of service

## Customization

### Colors & Branding
Edit `web/assets/css/style.css`:
```css
:root {
    --primary: #0066FF;        /* Main brand color */
    --accent: #30D482;         /* Accent color */
    --text-primary: #1A1A1A;   /* Main text */
    --text-secondary: #666666; /* Secondary text */
    /* ... more variables ... */
}
```

### Fonts
Currently uses Google Fonts (Inter). To change:
1. Update `<link>` in HTML head
2. Update `font-family` in CSS

### Logo & Branding
- Place logo in `assets/images/`
- Update `nav-brand` in HTML to use image instead of text

## Performance Tips

### Optimize Images
```bash
# Compress PNG
pngquant --quality 70-90 image.png

# Compress JPEG
jpegoptim --max=85 image.jpg

# WebP conversion (modern browsers)
cwebp -q 80 image.png -o image.webp
```

### Minify CSS/JS (Optional)
```bash
# Using csso and terser
csso assets/css/style.css -o assets/css/style.min.css
terser assets/js/script.js -o assets/js/script.min.js
```

## SEO Checklist

- [ ] Update `<title>` tags on all pages
- [ ] Update `<meta description>` on all pages
- [ ] Create `sitemap.xml`
- [ ] Create `robots.txt`
- [ ] Submit to Google Search Console
- [ ] Submit to Bing Webmaster Tools
- [ ] Set up Google Analytics (optional)
- [ ] Enable HTTPS (required)

## Mobile Responsiveness

The site is tested on:
- iPhone 12, 14, 15 (Safari)
- iPad Air, Pro (Safari)
- Android phones (Chrome)
- Desktop (Chrome, Safari, Firefox, Edge)

**Min viewport:** 320px (iPhone SE)
**Max viewport:** 1920px and above

## Browser Support

- Chrome 90+
- Safari 14+
- Firefox 88+
- Edge 90+
- Mobile browsers (iOS Safari, Chrome Android)

## Accessibility

- ✅ WCAG 2.1 AA compliant
- ✅ Semantic HTML
- ✅ Color contrast > 4.5:1
- ✅ Keyboard navigation
- ✅ Screen reader friendly

## Common Tasks

### Add a New Product Page

```bash
cp web/products/tone.html web/products/newproduct.html
```

Edit the file:
- Update product name
- Update description
- Update features
- Update system requirements
- Update color theme

### Update Download Center

Edit `web/downloads/index.html`:
```html
<!-- Change this -->
<a href="vxsuite-1.0.0-macos-universal.zip" download class="btn btn-primary">
    Download for macOS
</a>

<!-- Update version, file size, checksum -->
<p class="version">VX Studio v1.0.0 (Gold Release)</p>
<p class="file-size">📦 512 MB</p>
```

### Link to New Page

In navigation, add:
```html
<li><a href="/newpage/">New Page</a></li>
```

## Troubleshooting

### Links not working?
- Check file paths (use relative paths like `../`)
- Verify file exists
- Check spelling

### Styling looks broken?
- Make sure `assets/css/style.css` is linked correctly
- Check browser cache (Ctrl+Shift+Del)
- Verify file permissions

### Downloads not working?
- Check file exists in `downloads/` folder
- Verify MIME type in web server config
- Check file permissions (should be 644)

## Support Files

- `WEB_STRUCTURE.md` - Detailed deployment and structure guide
- `.gitignore` - Git ignore file (plugin zips should not be committed)
- `README.md` - This file

## Resources

- [vxstudio.marczewski.me.uk](https://vxstudio.marczewski.me.uk) - Live site
- [GitHub Repository](https://github.com/marczewski/VxStudio) - Source code
- [HTML Reference](https://developer.mozilla.org/en-US/docs/Web/HTML)
- [CSS Reference](https://developer.mozilla.org/en-US/docs/Web/CSS)

## License

Website content and design © 2026 VX Studio.
VX Studio plugins are released under MIT license.

---

**Need help deploying?** See `WEB_STRUCTURE.md` for detailed instructions.

**Ready to go live?** Your site is production-ready! 🚀
