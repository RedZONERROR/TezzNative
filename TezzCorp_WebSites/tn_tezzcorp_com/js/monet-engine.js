(() => {
    const STORAGE_SEED_KEY = 'tezzcorp.monet.seed';
    const STORAGE_GROUP_KEY = 'tezzcorp.monet.group';
    const DEFAULT_GROUP_ID = 'corporate_blue';

    const PALETTE_GROUPS = [
        { id: 'corporate_blue', name: 'Corporate Blue', description: 'Stable enterprise tone', seed: '#0f4fa8' },
        { id: 'velocity_red', name: 'Velocity Red', description: 'High-energy growth feel', seed: '#b2203a' },
        { id: 'executive_crimson', name: 'Executive Crimson', description: 'Premium red blend', seed: '#8f1c2e' },
        { id: 'ruby_charcoal', name: 'Ruby Charcoal', description: 'Red with deep neutral', seed: '#a62830' },
        { id: 'emerald_teal', name: 'Emerald Teal', description: 'Balanced operations look', seed: '#0f6d74' },
        { id: 'indigo_steel', name: 'Indigo Steel', description: 'Corporate modern blend', seed: '#1f4f82' },
        { id: 'sunset_orange', name: 'Sunset Orange', description: 'Warm product tone', seed: '#b95a24' },
    ];

    const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

    const normalizeHex = (hex) => {
        if (typeof hex !== 'string') return null;
        let value = hex.trim().replace(/^#/, '');
        if (value.length === 3) {
            value = value.split('').map((char) => char + char).join('');
        }
        if (!/^[0-9a-fA-F]{6}$/.test(value)) return null;
        return `#${value.toLowerCase()}`;
    };

    const hexToRgb = (hex) => {
        const normalized = normalizeHex(hex);
        if (!normalized) return null;
        const value = parseInt(normalized.slice(1), 16);
        return {
            r: (value >> 16) & 255,
            g: (value >> 8) & 255,
            b: value & 255,
        };
    };

    const rgbToHsl = ({ r, g, b }) => {
        const rn = r / 255;
        const gn = g / 255;
        const bn = b / 255;
        const max = Math.max(rn, gn, bn);
        const min = Math.min(rn, gn, bn);
        const delta = max - min;
        let h = 0;
        let s = 0;
        const l = (max + min) / 2;

        if (delta !== 0) {
            if (max === rn) h = ((gn - bn) / delta) % 6;
            else if (max === gn) h = (bn - rn) / delta + 2;
            else h = (rn - gn) / delta + 4;

            h = Math.round(h * 60);
            if (h < 0) h += 360;
            s = delta / (1 - Math.abs(2 * l - 1));
        }

        return {
            h,
            s: Math.round(s * 100),
            l: Math.round(l * 100),
        };
    };

    const hsl = (h, s, l, alpha = null) => {
        const hue = ((h % 360) + 360) % 360;
        const sat = clamp(Math.round(s), 0, 100);
        const lig = clamp(Math.round(l), 0, 100);
        if (alpha === null) return `hsl(${hue} ${sat}% ${lig}%)`;
        return `hsl(${hue} ${sat}% ${lig}% / ${clamp(alpha, 0, 1)})`;
    };

    const buildPalette = (seedHex, darkMode) => {
        const rgb = hexToRgb(seedHex) || hexToRgb('#0f4fa8');
        const base = rgbToHsl(rgb);
        const h = base.h;

        if (darkMode) {
            return {
                '--primary-color': hsl(h, base.s + 14, 74),
                '--secondary-color': hsl(h + 18, base.s + 10, 66),
                '--accent-color': hsl(h - 24, 70, 62),
                '--text-color': hsl(h + 24, 36, 93),
                '--text-muted': hsl(h + 18, 22, 73),
                '--text-light': hsl(h + 18, 22, 73),
                '--bg-color': hsl(h + 20, 45, 8),
                '--bg-elevated': hsl(h + 22, 38, 14),
                '--card-bg': hsl(h + 22, 36, 16),
                '--border-color': hsl(h + 18, 28, 27),
                '--gradient': `linear-gradient(132deg, ${hsl(h, base.s + 20, 58)} 0%, ${hsl(h + 16, base.s + 16, 52)} 54%, ${hsl(h - 24, 74, 54)} 100%)`,
                '--gradient-hover': `linear-gradient(132deg, ${hsl(h, base.s + 20, 52)} 0%, ${hsl(h + 16, base.s + 16, 47)} 54%, ${hsl(h - 24, 74, 49)} 100%)`,
                '--gradient-alt': `linear-gradient(130deg, ${hsl(h + 18, 42, 16)} 0%, ${hsl(h, base.s + 12, 34)} 100%)`,
                '--gradient-text': `linear-gradient(120deg, ${hsl(h, base.s + 22, 78)} 0%, ${hsl(h + 16, base.s + 18, 72)} 56%, ${hsl(h - 24, 80, 72)} 100%)`,
                '--monet-orb-a': hsl(h - 18, 80, 58, 0.18),
                '--monet-orb-b': hsl(h + 18, 80, 60, 0.16),
                '--monet-orb-c': hsl(h + 38, 74, 56, 0.12),
                '--monet-header-line': hsl(h + 6, 88, 76, 0.86),
                '--nav-link-hover': hsl(h + 4, 88, 70, 0.14),
                '--nav-link-active': hsl(h + 4, 88, 72, 0.22),
            };
        }

        return {
            '--primary-color': hsl(h, base.s + 10, 40),
            '--secondary-color': hsl(h + 18, base.s + 6, 42),
            '--accent-color': hsl(h - 22, 72, 40),
            '--text-color': hsl(h + 20, 32, 15),
            '--text-muted': hsl(h + 18, 20, 38),
            '--text-light': hsl(h + 18, 20, 38),
            '--bg-color': hsl(h + 14, 34, 97),
            '--bg-elevated': hsl(h + 12, 30, 100),
            '--card-bg': hsl(h + 12, 28, 100),
            '--border-color': hsl(h + 14, 24, 86),
            '--gradient': `linear-gradient(132deg, ${hsl(h, base.s + 10, 40)} 0%, ${hsl(h + 20, base.s + 8, 44)} 54%, ${hsl(h - 22, 72, 42)} 100%)`,
            '--gradient-hover': `linear-gradient(132deg, ${hsl(h, base.s + 10, 35)} 0%, ${hsl(h + 20, base.s + 8, 39)} 54%, ${hsl(h - 22, 72, 37)} 100%)`,
            '--gradient-alt': `linear-gradient(130deg, ${hsl(h + 20, 46, 25)} 0%, ${hsl(h, base.s + 10, 38)} 100%)`,
            '--gradient-text': `linear-gradient(120deg, ${hsl(h, base.s + 12, 42)} 0%, ${hsl(h + 20, base.s + 10, 46)} 56%, ${hsl(h - 22, 72, 44)} 100%)`,
            '--monet-orb-a': hsl(h - 14, 78, 52, 0.16),
            '--monet-orb-b': hsl(h + 16, 80, 54, 0.14),
            '--monet-orb-c': hsl(h + 36, 78, 52, 0.11),
            '--monet-header-line': hsl(h + 8, 88, 50, 0.9),
            '--nav-link-hover': hsl(h + 6, 90, 46, 0.1),
            '--nav-link-active': hsl(h + 6, 90, 44, 0.18),
        };
    };

    const getPaletteGroup = (groupId) => PALETTE_GROUPS.find((item) => item.id === groupId) || PALETTE_GROUPS[0];

    const findGroupBySeed = (seedHex) => {
        const normalized = normalizeHex(seedHex);
        if (!normalized) return null;
        return PALETTE_GROUPS.find((item) => normalizeHex(item.seed) === normalized) || null;
    };

    const ensureAmbientLayer = () => {
        if (document.querySelector('.monet-ambient')) return;
        const layer = document.createElement('div');
        layer.className = 'monet-ambient';
        layer.setAttribute('aria-hidden', 'true');
        layer.innerHTML = `
            <span class="monet-orb orb-a"></span>
            <span class="monet-orb orb-b"></span>
            <span class="monet-orb orb-c"></span>
        `;
        document.body.prepend(layer);
    };

    const applyPalette = (seedHex, darkMode) => {
        const root = document.documentElement;
        const seed = normalizeHex(seedHex) || normalizeHex(getPaletteGroup(DEFAULT_GROUP_ID).seed);
        const palette = buildPalette(seed, darkMode);
        Object.entries(palette).forEach(([name, value]) => root.style.setProperty(name, value));
        root.style.setProperty('--monet-seed', seed);
        root.dataset.monetReady = 'true';
    };

    const resolveSeed = () => {
        const storedSeed = normalizeHex(localStorage.getItem(STORAGE_SEED_KEY) || '');
        if (storedSeed) return storedSeed;
        const storedGroup = localStorage.getItem(STORAGE_GROUP_KEY);
        if (storedGroup) return normalizeHex(getPaletteGroup(storedGroup).seed);
        const metaSeed = normalizeHex(document.querySelector('meta[name="theme-seed"]')?.getAttribute('content') || '');
        if (metaSeed) return metaSeed;
        return normalizeHex(getPaletteGroup(DEFAULT_GROUP_ID).seed);
    };

    const resolveGroupId = () => {
        const storedGroup = localStorage.getItem(STORAGE_GROUP_KEY);
        if (storedGroup && getPaletteGroup(storedGroup)) return storedGroup;
        const bySeed = findGroupBySeed(resolveSeed());
        return bySeed ? bySeed.id : DEFAULT_GROUP_ID;
    };

    const emitPaletteChange = (groupId, seed) => {
        window.dispatchEvent(new CustomEvent('tezz:palette-change', { detail: { groupId, seed } }));
    };

    const refresh = () => {
        const body = document.body;
        if (!body) return;
        const darkMode = body.classList.contains('dark-mode');
        const seed = resolveSeed();
        applyPalette(seed, darkMode);
        ensureAmbientLayer();
        emitPaletteChange(resolveGroupId(), seed);
    };

    const applySeed = (seedHex) => {
        const normalized = normalizeHex(seedHex);
        if (!normalized) return false;
        localStorage.setItem(STORAGE_SEED_KEY, normalized);
        const knownGroup = findGroupBySeed(normalized);
        if (knownGroup) localStorage.setItem(STORAGE_GROUP_KEY, knownGroup.id);
        else localStorage.removeItem(STORAGE_GROUP_KEY);
        refresh();
        return true;
    };

    const applyGroup = (groupId) => {
        const group = getPaletteGroup(groupId);
        if (!group) return false;
        localStorage.setItem(STORAGE_GROUP_KEY, group.id);
        localStorage.setItem(STORAGE_SEED_KEY, normalizeHex(group.seed));
        refresh();
        return true;
    };

    const getPaletteGroups = () =>
        PALETTE_GROUPS.map((group) => {
            const rgb = hexToRgb(group.seed);
            const base = rgbToHsl(rgb);
            return {
                ...group,
                swatches: [
                    hsl(base.h, base.s + 10, 44),
                    hsl(base.h + 16, base.s + 8, 47),
                    hsl(base.h - 22, 72, 43),
                ],
            };
        });

    window.MonetThemeEngine = {
        refresh,
        apply: applySeed,
        applyGroup,
        getSeed: resolveSeed,
        getCurrentGroup: resolveGroupId,
        getPaletteGroups,
    };

    document.addEventListener('DOMContentLoaded', refresh);
    window.addEventListener('tezz:theme-change', refresh);
})();
