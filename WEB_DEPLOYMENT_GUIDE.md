# VX Studio Website - Complete Deployment Guide

## Summary

A professional, modern website for VX Studio has been created in the `web/` folder. It's ready to deploy to **vxstudio.marczewski.me.uk** and includes everything needed for a professional plugin distribution site.

## What's Included

### Website Structure
```
web/
├── index.html                 # Home page (hero + product grid + features)
├── assets/
│   ├── css/style.css         # Modern, responsive styling
│   ├── js/script.js          # Smooth scroll, animations
│   └── images/               # (Ready for logos, screenshots)
├── downloads/                # ← PLUGIN FILES GO HERE
│   └── index.html            # Download center page
├── products/                 # (Template for individual products)
├── docs/                     # (Documentation hub)
├── support/                  # (Support & FAQ)
├── legal/                    # (License, privacy, terms)
├── README.md                 # Quick start guide
└── WEB_STRUCTURE.md          # Detailed technical guide
```

### Pages Included

1. **Home** - Beautiful landing page with:
   - Hero section
   - Product grid (12 plugins with descriptions)
   - Features section (6 key points)
   - Download CTA
   - Technology section
   - Documentation links

2. **Downloads** - Professional download center with:
   - macOS (Available now)
   - Windows (Coming soon with timeline)
   - System requirements
   - Installation instructions
   - Release notes
   - Version history

3. **Products** - (Template structure ready for 12 individual product pages)

4. **Documentation** - (Hub for user guides and technical docs)

5. **Support** - (FAQ and troubleshooting)

6. **Legal** - (License, privacy, terms of service)

## Where Plugin Files Should Live

### For Immediate Deployment (macOS Only)

1. **Build the plugins:**
   ```bash
   cd /Users/andrzejmarczewski/Documents/GitHub/VxStudio
   cmake --build build -j$(nproc)
   ```

2. **Create the distribution package:**
   ```bash
   # Create folder structure
   mkdir -p vxsuite-dist/vxsuite-1.0.0-macos-universal/{VST3,AudioUnits}

   # Copy built plugins
   cp -r build/*VST3/*.vst3 vxsuite-dist/vxsuite-1.0.0-macos-universal/VST3/
   cp -r build/*AU/*.component vxsuite-dist/vxsuite-1.0.0-macos-universal/AudioUnits/

   # Add documentation
   cp LICENSE.txt vxsuite-dist/vxsuite-1.0.0-macos-universal/
   echo "Installation instructions..." > vxsuite-dist/vxsuite-1.0.0-macos-universal/INSTALL.txt

   # Create ZIP
   cd vxsuite-dist
   zip -r vxsuite-1.0.0-macos-universal.zip vxsuite-1.0.0-macos-universal/
   ```

3. **Copy to web folder:**
   ```bash
   cp vxsuite-1.0.0-macos-universal.zip web/downloads/
   sha256sum vxsuite-1.0.0-macos-universal.zip > web/downloads/checksums.txt
   ```

4. **Update download page:**
   - Edit `web/downloads/index.html`
   - Update version number (if different from 1.0.0)
   - Update file size
   - Update SHA256 checksum
   - Update release notes

### File Size Expectations

- Each VST3 bundle: ~3-5 MB
- Each Audio Unit: ~3-5 MB
- Total uncompressed (12×2): ~80-120 MB
- ZIP compressed: ~40-60 MB (50-60% compression)

### File Naming Convention

**macOS:**
```
vxsuite-1.0.0-macos-universal.zip
├── vxsuite-1.0.0-macos-universal/
│   ├── VST3/
│   │   ├── VXTone.vst3/
│   │   ├── VXCleanup.vst3/
│   │   └── ... (12 total)
│   ├── AudioUnits/
│   │   ├── VXTone.component/
│   │   └── ... (12 total)
│   ├── INSTALL.txt
│   ├── README.txt
│   └── LICENSE.txt
```

**Windows (Future):**
```
vxsuite-1.0.0-windows-x64.zip
├── vxsuite-1.0.0-windows-x64/
│   ├── VST3/
│   │   ├── VXTone.vst3
│   │   └── ... (12 total)
│   ├── installer.exe (optional)
│   ├── INSTALL.txt
│   └── LICENSE.txt
```

## Deployment Options

### Option 1: Self-Hosted (Recommended for Full Control)

**Requirements:**
- Dedicated server or VPS (DigitalOcean, Linode, etc.)
- SSH access
- Web server (nginx recommended)

**Setup:**
```bash
# SSH into server
ssh user@vxstudio.marczewski.me.uk

# Create directory
mkdir -p /var/www/vxstudio

# Upload files via rsync
rsync -avz --delete ~/VxStudio/web/ user@vxstudio.marczewski.me.uk:/var/www/vxstudio/

# Set permissions
sudo chown -R www-data:www-data /var/www/vxstudio
sudo chmod -R 755 /var/www/vxstudio
```

**nginx Configuration:**
```nginx
server {
    server_name vxstudio.marczewski.me.uk;
    root /var/www/vxstudio;

    location / {
        try_files $uri $uri/ =404;
        expires 1d;
        add_header Cache-Control "public, immutable";
    }

    location /downloads {
        client_max_body_size 1G;
        expires 7d;
    }

    # Redirect HTTP to HTTPS
    listen 80;
    return 301 https://$server_name$request_uri;
}

# HTTPS configuration
server {
    server_name vxstudio.marczewski.me.uk;
    listen 443 ssl http2;

    root /var/www/vxstudio;

    ssl_certificate /etc/letsencrypt/live/vxstudio.marczewski.me.uk/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/vxstudio.marczewski.me.uk/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;
    ssl_prefer_server_ciphers on;

    # ... rest of configuration
}
```

### Option 2: Cloudflare Pages (Easiest, Free)

**Setup:**
1. Push `web/` folder to GitHub
2. Log into Cloudflare
3. Go to Pages → Create a project
4. Connect GitHub repo
5. Build settings:
   - Build command: (leave empty - no build needed)
   - Build output directory: `web`
6. Deploy!

**Pros:**
- ✅ Free SSL/HTTPS
- ✅ Global CDN
- ✅ Auto-deploys on git push
- ✅ No server to manage

**Cons:**
- Must keep files in GitHub (plugin zips should be large file storage)

### Option 3: Netlify (Also Easy, Free)

1. Push `web/` to GitHub
2. Log into Netlify
3. Create new site → Connect to Git
4. Select repo
5. Build settings:
   - Build command: (leave empty)
   - Publish directory: `web`
6. Deploy!

Similar to Cloudflare Pages. Both are excellent choices.

## Important: Large File Management

### GitHub vs. Web Server

**Do NOT commit plugin zips to GitHub.** Instead:

1. **On GitHub:**
   - Include only `.gitignore` entry for `downloads/*.zip`
   - Keep website source only

2. **On Web Server:**
   - Upload plugin zips directly via rsync/SFTP
   - Or use GitHub Releases for binary distribution

### Recommended: GitHub Releases

```bash
# Create release on GitHub with plugin attachments
gh release create v1.0.0 web/downloads/vxsuite-1.0.0-macos-universal.zip

# Update download link in HTML:
# href="https://github.com/marczewski/VxStudio/releases/download/v1.0.0/vxsuite-1.0.0-macos-universal.zip"
```

## SSL/HTTPS (Required)

All downloads must be served over HTTPS.

### Using Let's Encrypt (Free)

```bash
# Install certbot
sudo apt-get install certbot python3-certbot-nginx

# Get certificate
sudo certbot certonly --nginx -d vxstudio.marczewski.me.uk

# Auto-renewal
sudo systemctl enable certbot.timer
sudo systemctl start certbot.timer
```

### Certificate Renewal
```bash
sudo certbot renew --quiet  # Runs daily via cron
```

## Deployment Checklist

- [ ] Build plugins successfully
- [ ] Create distribution ZIP file
- [ ] Generate SHA256 checksums
- [ ] Copy ZIP to `web/downloads/`
- [ ] Update `web/downloads/index.html` with:
  - [ ] Version number
  - [ ] File size
  - [ ] SHA256 checksum
  - [ ] Release notes
- [ ] Test website locally: `python3 -m http.server 8000`
- [ ] Test all links work
- [ ] Test download links
- [ ] Choose deployment method (self-hosted/Cloudflare/Netlify)
- [ ] Upload to server/deploy
- [ ] Test live site
- [ ] Enable HTTPS
- [ ] Test HTTPS works
- [ ] Submit sitemap to Google Search Console
- [ ] Submit to Bing Webmaster Tools
- [ ] Announce on social media/email list

## Post-Deployment

### Monitor Downloads
```bash
# Count downloads from nginx logs
grep "/downloads/" /var/log/nginx/access.log | wc -l

# Check for broken links
grep "404" /var/log/nginx/error.log
```

### Update Content
Any time you:
- Release a new version
- Update documentation
- Change product descriptions

**Edit files, then:**
```bash
rsync -avz --delete ~/VxStudio/web/ user@vxstudio.marczewski.me.uk:/var/www/vxstudio/
```

### Version Control
Keep `web/` folder in Git for version history:
```bash
cd /path/to/VxStudio
git add web/
git commit -m "Update website content"
git push
```

(But exclude large plugin zips via `.gitignore`)

## Windows Version (Future)

When you're ready to release Windows builds:

1. Build Windows VST3 plugins
2. Create installer (optional, NSIS recommended)
3. Create ZIP: `vxsuite-1.0.0-windows-x64.zip`
4. Copy to `web/downloads/`
5. Update `web/downloads/index.html`:
   - Change status from "Coming Soon" to "Available"
   - Add download button and file size
   - Remove countdown timer

6. Deploy updated website

## Support Resources

- **Website Guide:** `web/WEB_STRUCTURE.md` - Detailed technical documentation
- **Quick Start:** `web/README.md` - Getting started with the site
- **Main Website:** https://vxstudio.marczewski.me.uk

## Next Steps

1. **Build the plugins:**
   ```bash
   cd /Users/andrzejmarczewski/Documents/GitHub/VxStudio
   cmake --build build -j$(nproc)
   ```

2. **Create distribution:** Follow "Where Plugin Files Should Live" section above

3. **Choose deployment method:** Options 1, 2, or 3

4. **Deploy:** Follow deployment option instructions

5. **Test:** Verify everything works on live site

6. **Announce:** Email list, social media, etc.

---

## Summary

- ✅ **Website ready:** Professional, modern, responsive design
- ✅ **Deployment ready:** Works with self-hosted, Cloudflare, or Netlify
- ✅ **Plugin-ready:** `downloads/` folder ready for your plugin ZIPs
- ✅ **Scalable:** Easy to update content and release new versions
- ✅ **Professional:** Follows best practices from iZotope, Native Instruments, Waves

Your website is ready for gold release! 🚀

**Questions?** See `web/WEB_STRUCTURE.md` for complete technical documentation.
