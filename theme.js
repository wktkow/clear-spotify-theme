(async () => {
  // Early splash disable – hide overlay before UI even loads.
  // At document_start body may not exist yet, so wait for it.
  try {
    const earlySettings = JSON.parse(
      localStorage.getItem("clear-theme-settings") || "{}",
    );
    if (earlySettings.splashScreen === false) {
      const applyNoSplash = () =>
        document.body
          ? document.body.classList.add("clear-no-splash")
          : document.addEventListener(
              "DOMContentLoaded",
              () => document.body.classList.add("clear-no-splash"),
              { once: true },
            );
      applyNoSplash();
    }
  } catch {}

  // Wait for Spotify UI to be ready (works with and without Spicetify)
  while (!document.querySelector(".Root__main-view")) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }

  // --- DOM-based player helpers (no Spicetify dependency) ---
  function isPlaying() {
    const btn = document.querySelector(
      '[data-testid="control-button-playpause"]',
    );
    return btn?.getAttribute("aria-label") === "Pause";
  }

  function onPlayPauseChange(callback) {
    function attach() {
      const btn = document.querySelector(
        '[data-testid="control-button-playpause"]',
      );
      if (!btn) return false;
      new MutationObserver(() => callback()).observe(btn, {
        attributes: true,
        attributeFilter: ["aria-label"],
      });
      return true;
    }
    if (!attach()) {
      const retry = new MutationObserver(() => {
        if (attach()) retry.disconnect();
      });
      retry.observe(document.body, { childList: true, subtree: true });
    }
  }

  function onSongChange(callback) {
    let lastTrack = "";
    function check() {
      const el =
        document.querySelector('[data-testid="context-item-info-title"]') ||
        document.querySelector(".main-trackInfo-name");
      const title = el?.textContent || "";
      if (title && title !== lastTrack) {
        lastTrack = title;
        callback();
      }
    }
    function attach() {
      const bar = document.querySelector(
        '.Root__now-playing-bar, [data-testid="now-playing-bar"]',
      );
      if (!bar) return false;
      new MutationObserver(() => check()).observe(bar, {
        childList: true,
        subtree: true,
        characterData: true,
      });
      return true;
    }
    if (!attach()) {
      const retry = new MutationObserver(() => {
        if (attach()) retry.disconnect();
      });
      retry.observe(document.body, { childList: true, subtree: true });
    }
  }

  function getUsername() {
    // 1. Spicetify API (desktop) – most reliable
    try {
      const sp = window.Spicetify;
      const u =
        sp?.Platform?.UserAPI?._product_state?.username ||
        sp?.Platform?.Session?.username ||
        sp?.Platform?.username;
      if (u) return u;
    } catch {}

    // 2. User widget link with href (desktop <a> tag)
    const widgetLink = document.querySelector(
      '.main-userWidget-box a[href*="/user/"], [data-testid="user-widget-link"][href]',
    );
    if (widgetLink) {
      const m = widgetLink.getAttribute("href")?.match(/\/user\/([^/?#]+)/);
      if (m) return m[1];
    }

    // 3. Web: match the display name from user widget to a page link
    const userWidget = document.querySelector(
      '[data-testid="user-widget-link"]',
    );
    if (userWidget) {
      const displayName = userWidget.getAttribute("aria-label");
      if (displayName) {
        for (const a of document.querySelectorAll('a[href*="/user/"]')) {
          if (a.textContent.trim() === displayName) {
            const m = a.getAttribute("href")?.match(/\/user\/([^/?#]+)/);
            if (m) return m[1];
          }
        }
      }
    }

    // 4. Last resort: any user link on page (may be wrong user)
    const anyLink = document.querySelector('a[href*="/user/"]');
    if (anyLink) {
      const m = anyLink.getAttribute("href")?.match(/\/user\/([^/?#]+)/);
      if (m) return m[1];
    }
    return null;
  }

  const isGlobalNav = !!(
    document.querySelector(".globalNav") ||
    document.querySelector(".Root__globalNav") ||
    document.getElementById("global-nav-bar") ||
    document.querySelector(".main-globalNav-searchSection")
  );

  if (isGlobalNav) {
    document.body.classList.add("global-nav");
  } else {
    document.body.classList.add("control-nav");
  }

  // --- Clear Settings ---
  const CLEAR_SETTINGS_KEY = "clear-theme-settings";

  function loadSettings() {
    try {
      return JSON.parse(localStorage.getItem(CLEAR_SETTINGS_KEY)) || {};
    } catch {
      return {};
    }
  }

  function saveSettings(settings) {
    localStorage.setItem(CLEAR_SETTINGS_KEY, JSON.stringify(settings));
  }

  function applySettings() {
    const settings = loadSettings();
    document.body.classList.toggle(
      "clear-thick-bars",
      settings.thickBars !== false,
    );
    document.body.classList.toggle(
      "clear-nyan-cat",
      settings.nyanCat !== false,
    );
    document.body.classList.toggle("clear-sonic", settings.sonic !== false);
    document.body.classList.toggle(
      "clear-auto-now-playing",
      settings.autoNowPlaying !== false,
    );
    document.body.classList.toggle(
      "clear-pause-dim",
      settings.pauseDim !== false,
    );
    document.body.classList.toggle(
      "clear-hide-marketplace",
      settings.hideMarketplace !== false,
    );
    // Font choice: "default" | "inter" | "geist"
    const font = settings.fontChoice || "default";
    document.body.classList.toggle("clear-font-inter", font === "inter");
    document.body.classList.toggle("clear-font-geist", font === "geist");
    // Load the chosen font from Google Fonts
    const fontUrls = {
      inter:
        "https://fonts.googleapis.com/css2?family=Inter:wght@100..900&display=swap",
      geist:
        "https://fonts.googleapis.com/css2?family=Geist:wght@100..900&display=swap",
    };
    let fontLink = document.getElementById("clear-font-link");
    if (fontUrls[font]) {
      if (!fontLink) {
        fontLink = document.createElement("link");
        fontLink.id = "clear-font-link";
        fontLink.rel = "stylesheet";
        document.head.appendChild(fontLink);
      }
      if (fontLink.href !== fontUrls[font]) {
        fontLink.href = fontUrls[font];
      }
    } else if (fontLink) {
      fontLink.remove();
    }
  }

  applySettings();

  // --- Auto Now Playing View ---
  function isNowPlayingOpen() {
    return !!document.querySelector('aside[aria-label="Now playing view"]');
  }

  function clickNowPlayingButton() {
    const btn = document.querySelector(
      'button[data-restore-focus-key="now_playing_view"]',
    );
    if (btn) btn.click();
  }

  function initAutoNowPlaying() {
    // Close now playing view on app startup
    function tryCloseOnStartup(attempts) {
      if (isNowPlayingOpen()) {
        clickNowPlayingButton();
      } else if (attempts > 0) {
        setTimeout(() => tryCloseOnStartup(attempts - 1), 500);
      }
    }
    tryCloseOnStartup(10);

    // Open now playing view on each track change
    onSongChange(() => {
      const settings = loadSettings();
      if (settings.autoNowPlaying === false) return;
      setTimeout(() => {
        if (!isNowPlayingOpen()) {
          clickNowPlayingButton();
        }
      }, 1000);
    });

    // Open now playing view when playback starts
    onPlayPauseChange(() => {
      const settings = loadSettings();
      if (settings.autoNowPlaying === false) return;
      setTimeout(() => {
        if (isPlaying()) {
          if (!isNowPlayingOpen()) {
            clickNowPlayingButton();
          }
        } else {
          if (isNowPlayingOpen()) {
            clickNowPlayingButton();
          }
        }
      }, 1000);
    });
  }

  initAutoNowPlaying();

  // --- Pause state class on body ---
  function updatePausedState() {
    document.body.classList.toggle("clear-paused", !isPlaying());
  }
  updatePausedState();
  onPlayPauseChange(updatePausedState);

  function showMarketplaceWarning(onConfirm) {
    const overlay = document.createElement("div");
    overlay.className = "clear-warning-overlay clear-warning-overlay--open";

    const modal = document.createElement("div");
    modal.className = "clear-warning-modal";

    const icon = document.createElement("div");
    icon.className = "clear-warning-icon";
    icon.innerHTML = `<svg width="24" height="24" viewBox="0 0 24 24" fill="currentColor"><path d="M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z"/></svg>`;
    modal.appendChild(icon);

    const title = document.createElement("div");
    title.className = "clear-warning-title";
    title.textContent = "Warning";
    modal.appendChild(title);

    const msg = document.createElement("div");
    msg.className = "clear-warning-message";
    msg.textContent =
      "Most items from the Marketplace are not compatible with Clear and may cause errors that force a full reinstall. Are you sure you want to show the Marketplace?";
    modal.appendChild(msg);

    const actions = document.createElement("div");
    actions.className = "clear-warning-actions";

    const cancelBtn = document.createElement("button");
    cancelBtn.className = "clear-warning-btn clear-warning-btn--cancel";
    cancelBtn.textContent = "Keep Hidden";
    cancelBtn.addEventListener("click", () => {
      overlay.classList.remove("clear-warning-overlay--open");
      setTimeout(() => overlay.remove(), 150);
    });

    const confirmBtn = document.createElement("button");
    confirmBtn.className = "clear-warning-btn clear-warning-btn--confirm";
    confirmBtn.textContent = "Show Marketplace";
    confirmBtn.addEventListener("click", () => {
      overlay.classList.remove("clear-warning-overlay--open");
      setTimeout(() => overlay.remove(), 150);
      onConfirm();
    });

    actions.appendChild(cancelBtn);
    actions.appendChild(confirmBtn);
    modal.appendChild(actions);

    overlay.addEventListener("click", (e) => {
      if (e.target === overlay) {
        overlay.classList.remove("clear-warning-overlay--open");
        setTimeout(() => overlay.remove(), 150);
      }
    });

    overlay.appendChild(modal);
    document.body.appendChild(overlay);
  }

  function openSettingsModal() {
    // Don't double-create
    if (document.querySelector(".clear-settings-overlay")) {
      document
        .querySelector(".clear-settings-overlay")
        .classList.add("clear-settings-overlay--open");
      return;
    }

    const settings = loadSettings();

    const overlay = document.createElement("div");
    overlay.className = "clear-settings-overlay clear-settings-overlay--open";

    const modal = document.createElement("div");
    modal.className = "clear-settings-modal";

    // Header
    const header = document.createElement("div");
    header.className = "clear-settings-header";
    header.innerHTML = `<span class="clear-settings-title">Clear Settings</span>`;
    const closeBtn = document.createElement("button");
    closeBtn.className = "clear-settings-close";
    closeBtn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor"><path d="M3.293 3.293a1 1 0 0 1 1.414 0L12 10.586l7.293-7.293a1 1 0 1 1 1.414 1.414L13.414 12l7.293 7.293a1 1 0 0 1-1.414 1.414L12 13.414l-7.293 7.293a1 1 0 0 1-1.414-1.414L10.586 12 3.293 4.707a1 1 0 0 1 0-1.414"/></svg>`;
    closeBtn.addEventListener("click", () => {
      overlay.classList.remove("clear-settings-overlay--open");
      setTimeout(() => overlay.remove(), 150);
    });
    header.appendChild(closeBtn);
    modal.appendChild(header);

    // Toggle rows
    function addToggle(key, label, desc) {
      const isOn = settings[key] !== false;
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      row.innerHTML = `<div><div class="clear-settings-row-label">${label}</div><div class="clear-settings-row-desc">${desc}</div></div>`;
      const toggle = document.createElement("button");
      toggle.className =
        "clear-settings-toggle" + (isOn ? " clear-settings-toggle--on" : "");
      toggle.innerHTML = `<span class="clear-settings-toggle-knob"></span>`;
      toggle.addEventListener("click", () => {
        const s = loadSettings();
        const newVal = s[key] === false;
        s[key] = newVal;
        saveSettings(s);
        toggle.classList.toggle("clear-settings-toggle--on", newVal);
        applySettings();
      });
      row.appendChild(toggle);
      modal.appendChild(row);
    }

    addToggle(
      "thickBars",
      "Thick Progress Bars",
      "Make playback and volume bars thicker",
    );
    addToggle(
      "nyanCat",
      "Nyan Cat Progress Bar",
      "Rainbow progress bar with Nyan Cat slider",
    );
    addToggle("sonic", "Sonic Dancing", "Dancing Sonic above the progress bar");
    addToggle(
      "autoNowPlaying",
      "Auto Now Playing View",
      "Automatically open Now Playing panel on track change",
    );
    addToggle(
      "pauseDim",
      "Dim On Pause",
      "Dim Sonic and Nyan Cat when playback is paused",
    );
    addToggle(
      "splashScreen",
      "Startup Splash",
      "Show the clear. splash screen on startup",
    );
    addToggle(
      "lyricsFade",
      "Lyrics Fade",
      "Fade transition when opening or closing lyrics",
    );
    // Hide Marketplace toggle (desktop only) with warning modal
    {
      const isWeb =
        document.documentElement.classList.contains(
          "spotify__container--is-web",
        ) ||
        document.body?.classList.contains("spotify__container--is-web") ||
        document.querySelector(".spotify__container--is-web");
      if (!isWeb) {
        const key = "hideMarketplace";
        const label = "Hide Marketplace";
        const desc = "Hide the Marketplace link from the dropdown menu";
        const isOn = settings[key] !== false;
        const row = document.createElement("div");
        row.className = "clear-settings-row";
        row.innerHTML = `<div><div class="clear-settings-row-label">${label}</div><div class="clear-settings-row-desc">${desc}</div></div>`;
        const toggle = document.createElement("button");
        toggle.className =
          "clear-settings-toggle" + (isOn ? " clear-settings-toggle--on" : "");
        toggle.innerHTML = `<span class="clear-settings-toggle-knob"></span>`;
        toggle.addEventListener("click", () => {
          const s = loadSettings();
          const currentlyOn = s[key] !== false;
          if (currentlyOn) {
            // Turning OFF — show warning modal
            showMarketplaceWarning(() => {
              s[key] = false;
              saveSettings(s);
              toggle.classList.remove("clear-settings-toggle--on");
              applySettings();
            });
          } else {
            s[key] = true;
            saveSettings(s);
            toggle.classList.add("clear-settings-toggle--on");
            applySettings();
          }
        });
        row.appendChild(toggle);
        modal.appendChild(row);
      }
    }

    // Font dropdown
    {
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      row.innerHTML = `<div><div class="clear-settings-row-label">Font</div><div class="clear-settings-row-desc">Choose the app font</div></div>`;
      const select = document.createElement("select");
      select.className = "clear-settings-select";
      [
        { v: "default", l: "Spotify Default" },
        { v: "inter", l: "Inter" },
        { v: "geist", l: "Geist" },
      ].forEach((o) => {
        const opt = document.createElement("option");
        opt.value = o.v;
        opt.textContent = o.l;
        if ((settings.fontChoice || "default") === o.v) opt.selected = true;
        select.appendChild(opt);
      });
      select.addEventListener("change", () => {
        const s = loadSettings();
        s.fontChoice = select.value;
        saveSettings(s);
        applySettings();
      });
      row.appendChild(select);
      modal.appendChild(row);
    }

    // Visualizer Settings button
    {
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      row.innerHTML = `<div><div class="clear-settings-row-label">Audio Visualizer</div><div class="clear-settings-row-desc">Configure bars, framerate, and audio source</div></div>`;
      const openBtn = document.createElement("button");
      openBtn.className = "clear-settings-select";
      openBtn.textContent = "Settings →";
      openBtn.style.cursor = "pointer";
      openBtn.addEventListener("click", () => {
        openVisualizerSettings();
      });
      row.appendChild(openBtn);
      modal.appendChild(row);
    }

    // Close on overlay click
    overlay.addEventListener("click", (e) => {
      if (e.target === overlay) {
        overlay.classList.remove("clear-settings-overlay--open");
        setTimeout(() => overlay.remove(), 150);
      }
    });

    overlay.appendChild(modal);
    document.body.appendChild(overlay);
  }

  // --- 3-dots kebab menu replacing marketplace icon ---
  function initKebabMenu() {
    const navBar = document.getElementById("global-nav-bar");
    if (!navBar) return false;

    // Hide original custom navlinks (marketplace icon)
    const customNavContainer = navBar.querySelector(
      ".custom-navlinks-scrollable_container",
    );
    if (customNavContainer) {
      customNavContainer.style.display = "none";
    }

    // Don't double-inject
    if (navBar.querySelector(".clear-kebab-menu")) return true;

    // Create kebab menu wrapper
    const wrapper = document.createElement("div");
    wrapper.className = "clear-kebab-menu";

    // 3-dots button
    const btn = document.createElement("button");
    btn.className = "clear-kebab-btn";
    btn.setAttribute("aria-label", "More options");
    btn.innerHTML = `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor">
      <circle cx="8" cy="3" r="1.5"/>
      <circle cx="8" cy="8" r="1.5"/>
      <circle cx="8" cy="13" r="1.5"/>
    </svg>`;

    // Dropdown panel
    const dropdown = document.createElement("div");
    dropdown.className = "clear-kebab-dropdown";

    const isWeb =
      document.documentElement.classList.contains(
        "spotify__container--is-web",
      ) ||
      document.body?.classList.contains("spotify__container--is-web") ||
      document.querySelector(".spotify__container--is-web");

    // SPA-aware navigation helper – avoids full page reloads on web
    function navigateSPA(path) {
      if (window.Spicetify?.Platform?.History) {
        Spicetify.Platform.History.push(path);
        return;
      }
      // Web: create a link and click it – Spotify's SPA intercepts <a> clicks
      const a = document.createElement("a");
      a.href = path;
      a.style.cssText =
        "position:fixed;opacity:0;pointer-events:none;top:-9999px;";
      const root = document.getElementById("main") || document.body;
      root.appendChild(a);
      a.click();
      setTimeout(() => a.remove(), 100);
    }

    const items = [
      // Marketplace: desktop only (Spicetify extension, doesn't exist on web)
      ...(!isWeb
        ? [
            {
              label: "Marketplace",
              extraClass: "clear-kebab-item-marketplace",
              icon: `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M1 2.75A.75.75 0 0 1 1.75 2h12.5a.75.75 0 0 1 0 1.5H1.75A.75.75 0 0 1 1 2.75zm0 5A.75.75 0 0 1 1.75 7h12.5a.75.75 0 0 1 0 1.5H1.75A.75.75 0 0 1 1 7.75zm0 5a.75.75 0 0 1 .75-.75h12.5a.75.75 0 0 1 0 1.5H1.75a.75.75 0 0 1-.75-.75z"/></svg>`,
              action: () => navigateSPA("/marketplace"),
            },
          ]
        : []),
      {
        label: "Profile",
        icon: `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M8 1.5a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM3.5 4.5a4.5 4.5 0 1 1 9 0 4.5 4.5 0 0 1-9 0zM8 10c-3.037 0-5.5 1.343-5.5 3v1.5h11V13c0-1.657-2.463-3-5.5-3zm-7 3c0-2.761 3.134-4.5 7-4.5s7 1.739 7 4.5v2a.5.5 0 0 1-.5.5h-13a.5.5 0 0 1-.5-.5v-2z"/></svg>`,
        action: () => {
          const username = getUsername();
          if (username) navigateSPA(`/user/${username}`);
        },
      },
      {
        label: "Spotify Settings",
        icon: `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M8 0a8 8 0 1 0 0 16A8 8 0 0 0 8 0zm1.219 11.188c-.332.273-.754.5-1.219.5s-.887-.227-1.219-.5c-.332.273-.754.5-1.219.5-.912 0-1.562-.852-1.562-1.875s.65-1.875 1.562-1.875c.465 0 .887.227 1.219.5.332-.273.754-.5 1.219-.5s.887.227 1.219.5c.332-.273.754-.5 1.219-.5.912 0 1.562.852 1.562 1.875s-.65 1.875-1.562 1.875c-.465 0-.887-.227-1.219-.5zM8 3a1.5 1.5 0 1 1 0 3 1.5 1.5 0 0 1 0-3z"/></svg>`,
        action: () => navigateSPA("/preferences"),
      },
      {
        label: "Clear Settings",
        icon: `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M7.07 2.243a.75.75 0 0 1 1.36-.05l.47.93a1.75 1.75 0 0 0 1.6.97l1.04-.04a.75.75 0 0 1 .68.97l-.3 1a1.75 1.75 0 0 0 .47 1.82l.74.68a.75.75 0 0 1-.12 1.22l-.9.52a1.75 1.75 0 0 0-.86 1.7l.1 1.04a.75.75 0 0 1-.88.8l-1.02-.2a1.75 1.75 0 0 0-1.77.7l-.6.84a.75.75 0 0 1-1.21 0l-.6-.84a1.75 1.75 0 0 0-1.77-.71l-1.02.2a.75.75 0 0 1-.88-.8l.1-1.04a1.75 1.75 0 0 0-.86-1.7l-.9-.52a.75.75 0 0 1-.12-1.22l.74-.68a1.75 1.75 0 0 0 .47-1.82l-.3-1a.75.75 0 0 1 .68-.97l1.04.04a1.75 1.75 0 0 0 1.6-.97l.47-.93zM8 5.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM6.5 8a1.5 1.5 0 1 1 3 0 1.5 1.5 0 0 1-3 0z"/></svg>`,
        action: () => openSettingsModal(),
      },
      {
        label: "About Clear",
        icon: `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M8 0a8 8 0 1 1 0 16A8 8 0 0 1 8 0zm0 1.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13zm-.75 3a1 1 0 1 1 1.5 0 1 1 0 0 1-1.5 0zM7 7h2v5H7V7z"/></svg>`,
        action: () => {
          window.open(
            "https://github.com/wktkow/clear-spotify-client",
            "_blank",
          );
        },
      },
    ];

    items.forEach((item) => {
      const menuItem = document.createElement("button");
      menuItem.className =
        "clear-kebab-item" + (item.extraClass ? " " + item.extraClass : "");
      menuItem.innerHTML = `<span class="clear-kebab-item-icon">${item.icon}</span><span class="clear-kebab-item-label">${item.label}</span>`;
      menuItem.addEventListener("click", (e) => {
        e.stopPropagation();
        dropdown.classList.remove("clear-kebab-dropdown--open");
        item.action();
      });
      dropdown.appendChild(menuItem);
    });

    // Toggle dropdown on click
    btn.addEventListener("click", (e) => {
      e.stopPropagation();
      dropdown.classList.toggle("clear-kebab-dropdown--open");
    });

    // Close dropdown when clicking outside
    document.addEventListener("click", () => {
      dropdown.classList.remove("clear-kebab-dropdown--open");
    });

    wrapper.appendChild(btn);
    wrapper.appendChild(dropdown);
    navBar.prepend(wrapper);

    return true;
  }

  // Retry until nav bar is ready, then observe for re-renders
  function waitAndInit() {
    if (initKebabMenu()) {
      const navBar = document.getElementById("global-nav-bar");
      if (navBar) {
        new MutationObserver(() => initKebabMenu()).observe(navBar, {
          childList: true,
          subtree: true,
        });
      }
    } else {
      setTimeout(waitAndInit, 500);
    }
  }

  waitAndInit();

  // --- Search bar auto-hide timer ---
  function initSearchBarTimer() {
    const navBar = document.getElementById("global-nav-bar");
    if (!navBar) {
      setTimeout(initSearchBarTimer, 500);
      return;
    }

    let hideTimer = null;

    function showBar(delay) {
      clearTimeout(hideTimer);
      navBar.classList.add("clear-nav-forced");
      hideTimer = setTimeout(() => {
        navBar.classList.remove("clear-nav-forced");
      }, delay);
    }

    function hideBarNow() {
      clearTimeout(hideTimer);
      navBar.classList.remove("clear-nav-forced");
      // Blur the input so CSS :hover takes back over cleanly
      const input = navBar.querySelector("input");
      if (input) input.blur();
    }

    function attachListeners() {
      const input = navBar.querySelector("input");
      if (!input) return false;

      input.addEventListener("click", () => showBar(30000));
      input.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          hideBarNow();
        } else {
          showBar(30000);
        }
      });

      // Search icon click — find the submit/search button near the input
      const searchBtn = navBar.querySelector(
        'button[aria-label="Search"], button[data-testid="search-icon"]',
      );
      if (searchBtn) {
        searchBtn.addEventListener("click", () => hideBarNow());
      }

      return true;
    }

    // Input may not exist yet, observe for it
    if (!attachListeners()) {
      const obs = new MutationObserver(() => {
        if (attachListeners()) obs.disconnect();
      });
      obs.observe(navBar, { childList: true, subtree: true });
    }
  }

  initSearchBarTimer();

  // --- Lyrics fade animation ---
  // Covers React mount/unmount jank with a black overlay on the main content area.
  // Lyrics live inside .Root__main-view (NOT the right sidebar).
  // Detect whether lyrics are currently visible (desktop + web)
  function areLyricsOpen() {
    // Desktop: Spicetify lyrics extension class
    if (document.querySelector(".lyrics-lyrics-container")) return true;
    // Web: lyrics-cinema gets children when lyrics are shown
    const cinema = document.getElementById("lyrics-cinema");
    if (cinema && cinema.children.length > 0) return true;
    // Web: lyrics button active state
    const btn = document.querySelector('[data-testid="lyrics-button"]');
    if (btn && btn.getAttribute("data-active") === "true") return true;
    return false;
  }

  function initLyricsFade() {
    let animating = false;
    let lyricsWasOpen = areLyricsOpen();

    // Create overlay (will be inserted into the main view container)
    const overlay = document.createElement("div");
    overlay.id = "clear-lyrics-overlay";
    overlay.style.cssText =
      "position:absolute;inset:0;z-index:9999;background:#000;opacity:0;pointer-events:none;border-radius:var(--border-radius-md);";

    function ensureOverlay() {
      const mainView = document.querySelector(".Root__main-view");
      if (!mainView) return false;
      // Make main view a positioning context for the absolute overlay
      if (getComputedStyle(mainView).position === "static") {
        mainView.style.position = "relative";
      }
      if (!mainView.contains(overlay)) {
        mainView.appendChild(overlay);
      }
      return true;
    }

    // Raw rAF opacity stepper
    function stepOpacity(el, from, to, duration) {
      return new Promise((resolve) => {
        const start = performance.now();
        function tick(now) {
          const t = Math.min((now - start) / duration, 1);
          const e = t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
          el.style.opacity = String(from + (to - from) * e);
          if (t < 1) requestAnimationFrame(tick);
          else resolve();
        }
        requestAnimationFrame(tick);
      });
    }

    async function doReveal() {
      if (animating) return;
      if (loadSettings().lyricsFade === false) return;
      if (!ensureOverlay()) return;
      animating = true;
      // Snap black instantly over the main view
      overlay.style.opacity = "1";
      // Never set pointer-events:auto — overlay is purely visual, never blocks clicks
      // Wait for React to settle
      await new Promise((r) => setTimeout(r, 400));
      // Fade out to reveal
      await stepOpacity(overlay, 1, 0, 500);
      animating = false;
    }

    // Watch the DOM for lyrics state changes (desktop + web)
    const obs = new MutationObserver(() => {
      const lyricsNow = areLyricsOpen();
      if (lyricsNow !== lyricsWasOpen) {
        lyricsWasOpen = lyricsNow;
        doReveal();
      }
    });
    obs.observe(document.body, { childList: true, subtree: true });

    // Web: also observe the lyrics button data-active attribute
    function watchLyricsButton() {
      const btn = document.querySelector('[data-testid="lyrics-button"]');
      if (!btn) return false;
      new MutationObserver(() => {
        const lyricsNow = areLyricsOpen();
        if (lyricsNow !== lyricsWasOpen) {
          lyricsWasOpen = lyricsNow;
          doReveal();
        }
      }).observe(btn, { attributes: true, attributeFilter: ["data-active"] });
      return true;
    }
    if (!watchLyricsButton()) {
      const retry = new MutationObserver(() => {
        if (watchLyricsButton()) retry.disconnect();
      });
      retry.observe(document.body, { childList: true, subtree: true });
    }
  }

  initLyricsFade();

  // --- Visualizer Settings Modal ---
  function openVisualizerSettings() {
    if (document.querySelector(".clear-vis-settings-overlay")) {
      document
        .querySelector(".clear-vis-settings-overlay")
        .classList.add("clear-vis-settings-overlay--open");
      return;
    }

    const settings = loadSettings();

    const overlay = document.createElement("div");
    overlay.className =
      "clear-vis-settings-overlay clear-vis-settings-overlay--open";

    const modal = document.createElement("div");
    modal.className = "clear-vis-settings-modal";

    // Header
    const header = document.createElement("div");
    header.className = "clear-settings-header";
    header.innerHTML = `<span class="clear-settings-title">Visualizer Settings</span>`;
    const closeBtn = document.createElement("button");
    closeBtn.className = "clear-settings-close";
    closeBtn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor"><path d="M3.293 3.293a1 1 0 0 1 1.414 0L12 10.586l7.293-7.293a1 1 0 1 1 1.414 1.414L13.414 12l7.293 7.293a1 1 0 0 1-1.414 1.414L12 13.414l-7.293 7.293a1 1 0 0 1-1.414-1.414L10.586 12 3.293 4.707a1 1 0 0 1 0-1.414"/></svg>`;
    closeBtn.addEventListener("click", () => {
      overlay.classList.remove("clear-vis-settings-overlay--open");
      setTimeout(() => overlay.remove(), 150);
    });
    header.appendChild(closeBtn);
    modal.appendChild(header);

    // --- Bar Opacity slider ---
    {
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      const savedOpacity =
        settings.visBarOpacity != null ? settings.visBarOpacity : 1.0;
      row.innerHTML = `<div><div class="clear-settings-row-label">Bar Opacity</div><div class="clear-settings-row-desc">Transparency of the visualizer bars</div></div>`;
      const wrapper = document.createElement("div");
      wrapper.className = "clear-vis-slider-wrapper";
      const slider = document.createElement("input");
      slider.type = "range";
      slider.className = "clear-vis-slider";
      slider.min = "0.1";
      slider.max = "1.0";
      slider.step = "0.05";
      slider.value = String(savedOpacity);
      const valLabel = document.createElement("span");
      valLabel.className = "clear-vis-slider-value";
      valLabel.textContent = Number(savedOpacity).toFixed(2);
      slider.addEventListener("input", () => {
        const val = parseFloat(slider.value);
        valLabel.textContent = val.toFixed(2);
        const s = loadSettings();
        s.visBarOpacity = val;
        saveSettings(s);
        if (visualizerApi) visualizerApi.applyOpacity(val);
      });
      wrapper.appendChild(slider);
      wrapper.appendChild(valLabel);
      row.appendChild(wrapper);
      modal.appendChild(row);
    }

    // --- Framerate switches ---
    {
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      row.innerHTML = `<div><div class="clear-settings-row-label">Framerate</div><div class="clear-settings-row-desc">How often the bars update per second</div></div>`;
      const btnGroup = document.createElement("div");
      btnGroup.className = "clear-vis-fps-group";
      const savedFps = settings.visFps || 30;
      [24, 30, 60].forEach((fps) => {
        const btn = document.createElement("button");
        btn.className =
          "clear-vis-fps-btn" +
          (savedFps === fps ? " clear-vis-fps-btn--active" : "");
        btn.textContent = fps + " fps";
        btn.addEventListener("click", () => {
          btnGroup
            .querySelectorAll(".clear-vis-fps-btn")
            .forEach((b) => b.classList.remove("clear-vis-fps-btn--active"));
          btn.classList.add("clear-vis-fps-btn--active");
          const s = loadSettings();
          s.visFps = fps;
          saveSettings(s);
          if (visualizerApi) visualizerApi.setFps(fps);
        });
        btnGroup.appendChild(btn);
      });
      row.appendChild(btnGroup);
      modal.appendChild(row);
    }

    // --- Frequency Cutoff ---
    {
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      row.innerHTML = `<div><div class="clear-settings-row-label">Frequency Range</div><div class="clear-settings-row-desc">Upper cutoff point (50 Hz – selected)</div></div>`;
      const btnGroup = document.createElement("div");
      btnGroup.className = "clear-vis-fps-group";
      const savedFreq = settings.visFreqMax || 12000;
      [10000, 12000, 14000, 16000, 18000].forEach((freq) => {
        const btn = document.createElement("button");
        btn.className =
          "clear-vis-fps-btn" +
          (savedFreq === freq ? " clear-vis-fps-btn--active" : "");
        btn.textContent = freq / 1000 + "kHz";
        btn.addEventListener("click", () => {
          btnGroup
            .querySelectorAll(".clear-vis-fps-btn")
            .forEach((b) => b.classList.remove("clear-vis-fps-btn--active"));
          btn.classList.add("clear-vis-fps-btn--active");
          const s = loadSettings();
          s.visFreqMax = freq;
          saveSettings(s);
          if (visualizerApi) visualizerApi.setFreqMax(freq);
        });
        btnGroup.appendChild(btn);
      });
      row.appendChild(btnGroup);
      modal.appendChild(row);
    }

    // --- Bar Count ---
    {
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      row.innerHTML = `<div><div class="clear-settings-row-label">Bar Count</div><div class="clear-settings-row-desc">Number of frequency bands displayed</div></div>`;
      const btnGroup = document.createElement("div");
      btnGroup.className = "clear-vis-fps-group";
      const savedBars = settings.visBarCount || 72;
      [8, 16, 24, 36, 72, 100, 144].forEach((count) => {
        const btn = document.createElement("button");
        btn.className =
          "clear-vis-fps-btn" +
          (savedBars === count ? " clear-vis-fps-btn--active" : "");
        btn.textContent = String(count);
        btn.addEventListener("click", () => {
          btnGroup
            .querySelectorAll(".clear-vis-fps-btn")
            .forEach((b) => b.classList.remove("clear-vis-fps-btn--active"));
          btn.classList.add("clear-vis-fps-btn--active");
          const s = loadSettings();
          s.visBarCount = count;
          saveSettings(s);
          if (visualizerApi) visualizerApi.setBarCount(count);
        });
        btnGroup.appendChild(btn);
      });
      row.appendChild(btnGroup);
      modal.appendChild(row);
    }

    // --- Audio Source dropdown ---
    {
      const row = document.createElement("div");
      row.className = "clear-settings-row";
      row.innerHTML = `<div><div class="clear-settings-row-label">Audio Source</div><div class="clear-settings-row-desc">Which audio output to visualize</div></div>`;
      const select = document.createElement("select");
      select.className = "clear-settings-select";

      const savedSource = settings.audioSource || "";

      const defaultOpt = document.createElement("option");
      defaultOpt.value = "@DEFAULT_MONITOR@";
      defaultOpt.textContent = "System Default";
      if (!savedSource || savedSource === "@DEFAULT_MONITOR@")
        defaultOpt.selected = true;
      select.appendChild(defaultOpt);

      function populateSources(sources) {
        while (select.options.length > 1) select.remove(1);
        sources.forEach((src) => {
          const opt = document.createElement("option");
          opt.value = src.name;
          opt.textContent = src.desc || src.name;
          if (savedSource === src.name) opt.selected = true;
          select.appendChild(opt);
        });
      }

      if (visualizerApi) {
        populateSources(visualizerApi.getAudioSources());
        visualizerApi.onSourcesUpdated(populateSources);
        const origRemove = overlay.remove.bind(overlay);
        overlay.remove = () => {
          visualizerApi.removeSourceCallback(populateSources);
          origRemove();
        };
        visualizerApi.requestSources();
      }

      select.addEventListener("change", () => {
        const s = loadSettings();
        s.audioSource = select.value;
        saveSettings(s);
        if (visualizerApi) visualizerApi.setSource(select.value);
      });

      row.appendChild(select);
      modal.appendChild(row);
    }

    // Close on overlay click
    overlay.addEventListener("click", (e) => {
      if (e.target === overlay) {
        overlay.classList.remove("clear-vis-settings-overlay--open");
        setTimeout(() => overlay.remove(), 150);
      }
    });

    overlay.appendChild(modal);
    document.body.appendChild(overlay);
  }

  // --- Audio Visualizer ---
  // Connects to a native audio capture daemon via WebSocket on localhost:7700.
  // The daemon captures real audio output (PulseAudio/PipeWire on Linux,
  // WASAPI loopback on Windows), performs FFT, and sends 72 frequency bars.
  function initVisualizer() {
    const MAX_BAR_COUNT = 144;
    const WS_PORT = 7700;
    const WS_RECONNECT_MS = 2000;

    let barCount = loadSettings().visBarCount || 72;

    let active = false;
    let overlay = null;
    let barContainer = null;
    const barEls = []; // bar div elements, rebuilt on count change
    let msgEl = null;

    // WebSocket state
    let ws = null;
    let wsConnected = false;
    const wsData = new Float32Array(MAX_BAR_COUNT);
    let reconnectTimer = null;
    let lastMsgTime = 0;

    // Audio source management
    let audioSources = []; // [{name, desc},...] from daemon
    let audioSourceCallbacks = []; // listeners for source list updates

    // --- WebSocket connection to native audio capture ---
    function connectWs() {
      if (ws) {
        try {
          ws.close();
        } catch (e) {}
      }
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }

      try {
        ws = new WebSocket(`ws://127.0.0.1:${WS_PORT}`);
      } catch (e) {
        wsConnected = false;
        showMessage(
          "Audio bridge not running",
          "Start vis-capture to enable the visualizer.",
        );
        reconnectTimer = setTimeout(connectWs, WS_RECONNECT_MS);
        return;
      }
      ws.binaryType = "arraybuffer";

      ws.onopen = () => {
        wsConnected = true;
        hideMessage();
        console.log("[VIS] WebSocket connected to audio bridge");
        // Request available audio sources
        ws.send("GET_SOURCES");
        // Apply saved source if user previously selected one
        const s = loadSettings();
        if (s.audioSource) {
          ws.send("SET_SOURCE:" + s.audioSource);
        }
        // Apply saved FPS
        const fps = s.visFps || 30;
        ws.send("SET_FPS:" + fps);
        // Apply saved frequency range
        const freqMax = s.visFreqMax || 12000;
        ws.send("SET_FREQ_MAX:" + freqMax);
        // Apply saved bar count
        const bc = s.visBarCount || 72;
        ws.send("SET_BAR_COUNT:" + bc);
      };

      let frameCount = 0;
      ws.onmessage = async (e) => {
        try {
          // Text message = JSON command response from daemon
          if (typeof e.data === "string") {
            try {
              const msg = JSON.parse(e.data);
              if (msg.sources) {
                audioSources = msg.sources;
                console.log("[VIS] Got", audioSources.length, "audio sources");
                audioSourceCallbacks.forEach((cb) => cb(audioSources));
              }
              if (msg.sourceChanged) {
                console.log("[VIS] Source changed to:", msg.sourceChanged);
              }
              if (msg.sourceError) {
                console.warn("[VIS] Source error:", msg.sourceError);
              }
            } catch (je) {
              console.warn("[VIS] Bad JSON from daemon:", je);
            }
            return;
          }
          // Binary message = audio bar data
          let buf;
          if (e.data instanceof ArrayBuffer) {
            buf = e.data;
          } else if (e.data && typeof e.data.arrayBuffer === "function") {
            buf = await e.data.arrayBuffer();
          } else {
            return;
          }
          const data = new Float32Array(buf);
          const len = Math.min(barCount, data.length);
          for (let i = 0; i < len; i++) wsData[i] = data[i];
          lastMsgTime = performance.now();
          // Draw bars immediately on each snapshot from daemon
          drawBars();
          frameCount++;
          if (frameCount <= 3) {
            const sample = [data[0], data[5], data[11], data[17], data[23]];
            console.log(
              `[VIS] snapshot#${frameCount}: ${data.length} floats, sample:`,
              sample.map((v) => v.toFixed(4)).join(", "),
            );
          }
        } catch (err) {
          console.warn("[VIS] onmessage error:", err);
        }
      };

      ws.onclose = () => {
        wsConnected = false;
        if (active) {
          showMessage("Audio bridge disconnected", "Reconnecting...");
          reconnectTimer = setTimeout(connectWs, WS_RECONNECT_MS);
        }
      };

      ws.onerror = () => {
        try {
          ws.close();
        } catch (e) {}
      };
    }

    function disconnectWs() {
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
      if (ws) {
        try {
          ws.close();
        } catch (e) {}
        ws = null;
      }
      wsConnected = false;
    }

    // --- Button injection ---
    function injectButton() {
      if (document.querySelector(".clear-visualizer-btn")) return true;
      const lyricsBtn = document.querySelector('[data-testid="lyrics-button"]');
      if (!lyricsBtn) return false;

      const btn = document.createElement("button");
      btn.className = "clear-visualizer-btn";
      btn.setAttribute("aria-label", "Audio Visualizer");
      btn.title = "Audio Visualizer";
      btn.innerHTML = `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><rect x="1" y="6" width="2" height="4" rx="0.5"/><rect x="4.5" y="3" width="2" height="10" rx="0.5"/><rect x="8" y="5" width="2" height="6" rx="0.5"/><rect x="11.5" y="2" width="2" height="12" rx="0.5"/></svg>`;
      btn.addEventListener("click", toggle);

      lyricsBtn.after(btn);
      return true;
    }

    function waitForButton() {
      if (injectButton()) {
        const bar = document.querySelector(
          ".main-nowPlayingBar-nowPlayingBar, .Root__now-playing-bar",
        );
        if (bar) {
          new MutationObserver(() => injectButton()).observe(bar, {
            childList: true,
            subtree: true,
          });
        }
      } else {
        setTimeout(waitForButton, 500);
      }
    }

    // --- Message display ---
    function showMessage(title, text) {
      if (!msgEl) return;
      msgEl.style.display = "flex";
      msgEl.querySelector(".clear-visualizer-message-title").textContent =
        title;
      msgEl.querySelector(".clear-visualizer-message-text").textContent = text;
      if (barContainer) barContainer.style.display = "none";
    }

    function hideMessage() {
      if (msgEl) msgEl.style.display = "none";
      if (barContainer) barContainer.style.display = "flex";
    }

    // --- Overlay creation ---
    function ensureOverlay() {
      if (overlay && overlay.parentNode) return;
      const mainView = document.querySelector(".Root__main-view");
      if (!mainView) return;

      overlay = document.createElement("div");
      overlay.id = "clear-visualizer-overlay";

      // Bar container: flexbox row, bars aligned to bottom
      barContainer = document.createElement("div");
      barContainer.id = "clear-visualizer-bars";
      barContainer.style.cssText =
        "position:absolute;inset:0;display:none;" +
        "display:flex;align-items:flex-end;justify-content:center;" +
        "gap:6px;padding:16px;box-sizing:border-box;";

      // Create bar divs (dynamic count)
      const savedOpacity =
        loadSettings().visBarOpacity != null
          ? loadSettings().visBarOpacity
          : 1.0;
      barEls.length = 0;
      for (let i = 0; i < barCount; i++) {
        const bar = document.createElement("div");
        bar.style.cssText =
          "flex:1;min-width:0;height:2px;" +
          "background:white;opacity:" +
          savedOpacity +
          ";border-radius:3px 3px 0 0;";
        barContainer.appendChild(bar);
        barEls.push(bar);
      }
      overlay.appendChild(barContainer);

      msgEl = document.createElement("div");
      msgEl.className = "clear-visualizer-message";
      msgEl.innerHTML =
        `<div class="clear-visualizer-message-icon">` +
        `<svg width="48" height="48" viewBox="0 0 16 16" fill="currentColor"><rect x="1" y="6" width="2" height="4" rx="0.5" opacity="0.3"/><rect x="4.5" y="3" width="2" height="10" rx="0.5" opacity="0.3"/><rect x="8" y="5" width="2" height="6" rx="0.5" opacity="0.3"/><rect x="11.5" y="2" width="2" height="12" rx="0.5" opacity="0.3"/></svg>` +
        `</div>` +
        `<div class="clear-visualizer-message-title">Connecting…</div>` +
        `<div class="clear-visualizer-message-text">Waiting for audio capture bridge.</div>`;
      msgEl.style.display = "flex";
      overlay.appendChild(msgEl);

      const closeBtn = document.createElement("button");
      closeBtn.className = "clear-visualizer-close";
      closeBtn.innerHTML = `<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M3.293 3.293a1 1 0 0 1 1.414 0L12 10.586l7.293-7.293a1 1 0 1 1 1.414 1.414L13.414 12l7.293 7.293a1 1 0 0 1-1.414 1.414L12 13.414l-7.293 7.293a1 1 0 0 1-1.414-1.414L10.586 12 3.293 4.707a1 1 0 0 1 0-1.414"/></svg>`;
      closeBtn.addEventListener("click", toggle);
      overlay.appendChild(closeBtn);

      if (getComputedStyle(mainView).position === "static") {
        mainView.style.position = "relative";
      }
      mainView.appendChild(overlay);
    }

    // --- Update bar heights (called once per WebSocket snapshot) ---
    function drawBars() {
      for (let i = 0; i < barCount; i++) {
        const v = wsData[i];
        const pct = Math.max(0.3, v * 100); // min 0.3% so bars are visible
        barEls[i].style.height = pct + "%";
      }
    }

    // --- Toggle ---
    async function toggle() {
      active = !active;
      const btn = document.querySelector(".clear-visualizer-btn");
      if (active) {
        ensureOverlay();
        if (overlay) overlay.classList.add("clear-visualizer-overlay--open");
        if (btn) btn.classList.add("clear-visualizer-btn--active");
        wsData.fill(0);
        connectWs();
      } else {
        if (overlay) overlay.classList.remove("clear-visualizer-overlay--open");
        disconnectWs();
        if (btn) btn.classList.remove("clear-visualizer-btn--active");
      }
    }

    waitForButton();

    // Expose source management API for settings modal
    return {
      getAudioSources: () => audioSources,
      onSourcesUpdated: (cb) => {
        audioSourceCallbacks.push(cb);
        if (audioSources.length > 0) cb(audioSources);
      },
      removeSourceCallback: (cb) => {
        audioSourceCallbacks = audioSourceCallbacks.filter((c) => c !== cb);
      },
      setSource: (name) => {
        if (ws && wsConnected) {
          ws.send("SET_SOURCE:" + name);
        }
        const s = loadSettings();
        s.audioSource = name;
        saveSettings(s);
      },
      setFps: (fps) => {
        if (ws && wsConnected) {
          ws.send("SET_FPS:" + fps);
        }
      },
      setFreqMax: (freq) => {
        if (ws && wsConnected) {
          ws.send("SET_FREQ_MAX:" + freq);
        }
      },
      setBarCount: (count) => {
        barCount = count;
        // Rebuild bar divs to match new count
        if (barContainer) {
          barContainer.innerHTML = "";
          barEls.length = 0;
          const opacity =
            loadSettings().visBarOpacity != null
              ? loadSettings().visBarOpacity
              : 1.0;
          for (let i = 0; i < barCount; i++) {
            const bar = document.createElement("div");
            bar.style.cssText =
              "flex:1;min-width:0;height:2px;" +
              "background:white;opacity:" +
              opacity +
              ";border-radius:3px 3px 0 0;";
            barContainer.appendChild(bar);
            barEls.push(bar);
          }
          wsData.fill(0);
        }
        if (ws && wsConnected) {
          ws.send("SET_BAR_COUNT:" + count);
        }
      },
      applyOpacity: (val) => {
        barEls.forEach((el) => {
          el.style.opacity = String(val);
        });
      },
      // Fetch sources from daemon — uses the active visualizer WS if open,
      // otherwise opens a temporary connection just for the query.
      requestSources: () => {
        if (ws && wsConnected) {
          ws.send("GET_SOURCES");
          return;
        }
        // No active connection — open a temporary one
        try {
          const tmp = new WebSocket(`ws://127.0.0.1:${WS_PORT}`);
          tmp.binaryType = "arraybuffer";
          tmp.onopen = () => {
            tmp.send("GET_SOURCES");
          };
          tmp.onmessage = (e) => {
            // Ignore binary audio frames — we only care about the text response
            if (typeof e.data !== "string") return;
            try {
              const msg = JSON.parse(e.data);
              if (msg.sources) {
                audioSources = msg.sources;
                audioSourceCallbacks.forEach((cb) => cb(audioSources));
              }
            } catch {}
            // Got our sources — close the temp connection
            try {
              tmp.close();
            } catch {}
          };
          tmp.onerror = () => {
            try {
              tmp.close();
            } catch {}
          };
          // Safety: close after 3s no matter what
          setTimeout(() => {
            try {
              tmp.close();
            } catch {}
          }, 3000);
        } catch {}
      },
    };
  }

  const visualizerApi = initVisualizer();

  // --- Fade out startup splash (wait for full page load + images) ---
  if (loadSettings().splashScreen !== false) {
    if (document.readyState !== "complete") {
      await new Promise((resolve) =>
        window.addEventListener("load", resolve, { once: true }),
      );
    }
    // Wait for Spotify's React UI to finish drawing
    while (
      !document.querySelector(".Root__main-view") ||
      !document.querySelector(
        ".main-nowPlayingBar-nowPlayingBar, .Root__now-playing-bar",
      )
    ) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    // Extra buffer for images and final paints
    await new Promise((resolve) => setTimeout(resolve, 1500));

    // Show "clear." branding text
    const splashText = document.createElement("div");
    splashText.id = "clear-splash-text";
    splashText.textContent = "clear.";
    document.body.appendChild(splashText);

    // Fade text in over 0.6s, hold for 0.5s, then fade out
    splashText.classList.add("clear-text-in");
    await new Promise((resolve) => setTimeout(resolve, 600));
    await new Promise((resolve) => setTimeout(resolve, 500));
    splashText.classList.remove("clear-text-in");
    splashText.style.opacity = "1";
    splashText.classList.add("clear-text-out");
    await new Promise((resolve) => setTimeout(resolve, 600));
    splashText.remove();

    // Now fade out the black background over 1s
    document.body.classList.add("clear-bg-out");
  }
})();
