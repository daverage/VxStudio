# VX Studio Website Structure & Deployment Guide

## Overview

The VX Studio website is built as a static site (HTML/CSS/JS) that can be deployed to any web host. It's designed to be fast, responsive, and professional while following best practices from leading VST plugin companies.

## Directory Structure

```
web/
├── index.html                    # Main landing page
├── downloads/
│   └── index.html               # Download center (macOS/Windows)
├── products/
│   ├── tone.html                # Individual product pages
│   ├── cleanup.html
│   ├── denoiser.html
│   └── ... (10 more product pages)
├── docs/
│   ├── index.html               # Documentation hub
│   ├── installation.html        # Installation guide
│   ├── framework.html           # Framework documentation
│   └── user-guides/             # Individual plugin guides
├── support/
│   ├── index.html               # Support & FAQ
│   └── troubleshooting.html
├── legal/
│   ├── license.html             # MIT License
│   ├── privacy.html             # Privacy policy
│   └── terms.html               # Terms of service
├── assets/
│   ├── css/
│   │   └── style.css            # Main stylesheet
│   ├── js/
│   │   └── script.js            # Interactive features
│   ├── images/
│   │   ├── logos/
│   │   ├── screenshots/
│   │   ├── product-icons/
│   │   └── ...
│   └── fonts/                   # Custom fonts (if any)
└── downloads/                   # ← PLUGIN FILES GO HERE

```

## Plugin File Organization

### Where to Place the Actual Plugin Files

The website's `downloads/` folder structure should contain the built plugin binaries:

```
web/downloads/
├── vxsuite-1.0.0-macos-universal.zip       # Main distribution for macOS
├── vxsuite-1.0.0-windows-x64.zip           # Main distribution for Windows (future)
├── older-versions/                         # Archive of previous releases
│   └── vxsuite-0.9.0-beta-macos.zip
└── checksums.txt                           # SHA256 checksums for verification
```

### File Naming Convention

**macOS:**
```
vxsuite-{VERSION}-macos-universal.zip
Example: vxsuite-1.0.0-macos-universal.zip
Contains: 12 .vst3 bundles + 12 .component (Audio Units)
```

**Windows:**
```
vxsuite-{VERSION}-windows-x64.zip
Example: vxsuite-1.0.0-windows-x64.zip
Contains: VST3 installer + 12 .vst3 plugin files
```

### Contents of Distribution Zips

**macOS ZIP structure:**
```
vxsuite-1.0.0-macos-universal/
├── VST3/
│   ├── VXTone.vst3/
│   ├── VXCleanup.vst3/
│   ├── VXDenoiser.vst3/
│   └── ... (9 more)
├── AudioUnits/
│   ├── VXTone.component/
│   └── ... (11 more)
├── INSTALL.txt
├── README.txt
└── LICENSE.txt
```

**Windows ZIP structure:**
```
vxsuite-1.0.0-windows-x64/
├── installer.exe              # Optional: Windows installer
├── VST3/
│   ├── VXTone.vst3
│   ├── VXCleanup.vst3
│   └── ... (10 more)
├── INSTALL.txt
├── README.txt
└── LICENSE.txt
```

## Build & Release Process

### Step 1: Build Plugins

```bash
cd /Users/andrzejmarczewski/Documents/GitHub/VxStudio
cmake --build build -j$(nproc)
```

This stages VST3 bundles to `Source/vxsuite/vst/`

### Step 2: Package for Distribution

```bash
# macOS Universal Binary Package
cd build
./scripts/package-macos.sh

# This creates:
# - vxsuite-1.0.0-macos-universal.zip
```

### Step 3: Copy to Web Downloads Folder

```bash
cp vxsuite-1.0.0-macos-universal.zip web/downloads/
cp vxsuite-1.0.0-macos-universal.zip.sha256 web/downloads/checksums.txt
```

### Step 4: Update HTML

Edit `web/downloads/index.html`:
- Update version numbers
- Update file sizes
- Update release notes
- Update SHA256 checksums

### Step 5: Deploy

```bash
# Using rsync to vxstudio.marczewski.me.uk
rsync -avz --delete web/ user@vxstudio.marczewski.me.uk:/var/www/vxstudio/

# Or using sftp/scp for individual files
scp web/downloads/vxsuite-1.0.0-macos-universal.zip \
    user@vxstudio.marczewski.me.uk:/var/www/vxstudio/downloads/
```

## Website Features

### Navigation Structure
- **Home** - Landing page with hero, product grid, features, and CTA
- **Products** - Individual product pages with detailed descriptions
- **Downloads** - macOS (available), Windows (coming soon)
- **Documentation** - User guides, installation instructions, framework docs
- **Support** - FAQ, troubleshooting, contact options

### Responsive Design
- Mobile-first approach
- Tested on:
  - iPhone 12/14/15+ (Safari)
  - iPad Air/Pro (Safari)
  - Android devices (Chrome)
  - Desktop (Chrome, Safari, Firefox, Edge)
  - 1024px and below viewport

### Performance
- Pure HTML/CSS/JS (no build step required)
- No external CDN dependencies (fonts self-hosted recommended)
- Compressed image assets
- Minified CSS/JS (optional, can defer)
- Load time target: < 2 seconds

### SEO
- Proper meta tags (title, description, keywords)
- Semantic HTML5 structure
- Mobile-friendly viewport
- Open Graph tags (in header if social sharing needed)
- Sitemap.xml (generate and upload)

## Customization & Content

### Adding a Product Page

1. Create `web/products/{product-name}.html`
2. Copy structure from existing product page
3. Update:
   - Product name and description
   - Feature list
   - Use cases
   - System requirements
   - Links to user guide

### Updating Download Center

Edit `web/downloads/index.html`:
- File names and sizes
- SHA256 checksums
- Release notes
- Version history
- System requirements

### Adding Documentation

1. Create file in `web/docs/`
2. Follow same HTML structure as other pages
3. Link from navigation
4. Update breadcrumb trail

## Deployment Instructions

### Option A: Traditional Web Host (Recommended for Stability)

1. **SSH into your server:**
   ```bash
   ssh user@vxstudio.marczewski.me.uk
   ```

2. **Navigate to web root:**
   ```bash
   cd /var/www/vxstudio
   ```

3. **Upload files via rsync:**
   ```bash
   rsync -avz --delete ~/VxStudio/web/ user@vxstudio.marczewski.me.uk:/var/www/vxstudio/
   ```

4. **Set permissions:**
   ```bash
   sudo chown -R www-data:www-data /var/www/vxstudio
   sudo chmod -R 755 /var/www/vxstudio
   ```

5. **Configure web server (nginx/Apache):**
   ```nginx
   server {
       server_name vxstudio.marczewski.me.uk;
       root /var/www/vxstudio;

       location / {
           try_files $uri $uri/ =404;
       }

       location /downloads {
           # For large files
           client_max_body_size 1G;
       }
   }
   ```

### Option B: Cloudflare Pages (Easy for Static Sites)

1. Push `web/` folder to GitHub
2. Connect Cloudflare Pages to GitHub
3. Set build command: (leave empty - no build needed)
4. Set publish directory: `web`
5. Deploy automatically on push

### Option C: Netlify

1. Push `web/` folder to GitHub
2. Connect Netlify to GitHub repo
3. Set build command: (leave empty)
4. Set publish directory: `web`
5. Enable automatic deploys

## Security Considerations

### HTTPS Configuration
- **Required.** All downloads must use HTTPS.
- Use Let's Encrypt certificates (free)
- Auto-renewal via certbot

### File Integrity
- Generate SHA256 checksums for all downloads
- Display checksums on downloads page
- Provide verification instructions

### Privacy
- Include privacy policy page
- No analytics tracking (unless explicitly configured)
- No third-party tracking scripts

## Monitoring & Analytics (Optional)

If you want usage statistics without invading privacy:

1. **Server logs:** Check web server logs for download counts
2. **Minimal privacy:** Use Fathom Analytics or Plausible (privacy-first)
3. **No tracking:** Skip analytics entirely (users appreciate this)

## Search Engine Optimization (SEO)

### Sitemap
Generate `sitemap.xml`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
    <url><loc>https://vxstudio.marczewski.me.uk/</loc></url>
    <url><loc>https://vxstudio.marczewski.me.uk/downloads/</loc></url>
    <url><loc>https://vxstudio.marczewski.me.uk/docs/</loc></url>
    <!-- ... more URLs ... -->
</urlset>
```

### robots.txt
```
User-agent: *
Allow: /
Disallow: /admin/

Sitemap: https://vxstudio.marczewski.me.uk/sitemap.xml
```

## Support & Future Enhancements

### Potential Additions
- User testimonials/case studies section
- Video demonstrations (YouTube embeds)
- Interactive plugin comparisons
- Blog/news section
- Newsletter signup
- Support ticket system integration

### Planned Windows Release
When Windows version is ready:
1. Create build for Windows
2. Add to `web/downloads/`
3. Update download page status
4. Send email to mailing list subscribers

## File Size Reference

Typical distribution sizes:
- macOS VST3 bundle: ~2-5 MB per plugin
- macOS Audio Unit: ~2-5 MB per plugin
- Total for 12 plugins (both formats): ~80-120 MB
- ZIP compression ratio: ~50-60%
- Resulting download: ~40-60 MB

## Troubleshooting

### Downloads not working?
- Check file permissions (755 for files, 644 for directories)
- Verify MIME types in web server config
- Check file exists in correct location
- Verify URL in HTML matches actual file

### Plugins not found after install?
- Direct users to DAW plugin scanning
- Provide correct installation paths in guides
- Include troubleshooting page with common issues

### Slow downloads?
- Consider CDN for large files (Cloudflare is free)
- Compress ZIP more aggressively
- Offer split downloads (individual plugins vs. bundle)

## Contact & Support

For issues with the website:
- GitHub: https://github.com/marczewski/VxStudio/issues
- Website: https://vxstudio.marczewski.me.uk/support/
- Email: support@marczewski.me.uk

---

**Ready to deploy?** Start with Option A or B above. The site is production-ready and follows industry best practices from iZotope, Native Instruments, and Waves Audio websites.
