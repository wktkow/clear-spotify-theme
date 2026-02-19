(async () => {
  while (!Spicetify?.React || !Spicetify?.ReactDOM || !Spicetify?.Platform) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }

  const isGlobalNav = !!(
    Spicetify.Platform.version >= "1.2.46" ||
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
    const thickBars = settings.thickBars !== false; // default on
    document.body.classList.toggle("clear-thick-bars", thickBars);
  }

  applySettings();

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

    // Thick Bars toggle
    const thickBarsOn = settings.thickBars !== false;
    const row = document.createElement("div");
    row.className = "clear-settings-row";
    row.innerHTML = `<div><div class="clear-settings-row-label">Thick Progress Bars</div><div class="clear-settings-row-desc">Make playback and volume bars thicker</div></div>`;

    const toggle = document.createElement("button");
    toggle.className =
      "clear-settings-toggle" +
      (thickBarsOn ? " clear-settings-toggle--on" : "");
    toggle.innerHTML = `<span class="clear-settings-toggle-knob"></span>`;
    toggle.addEventListener("click", () => {
      const s = loadSettings();
      const newVal = s.thickBars === false; // flip
      s.thickBars = newVal;
      saveSettings(s);
      toggle.classList.toggle("clear-settings-toggle--on", newVal);
      applySettings();
    });
    row.appendChild(toggle);
    modal.appendChild(row);

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

    const items = [
      {
        label: "Marketplace",
        icon: `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M1 2.75A.75.75 0 0 1 1.75 2h12.5a.75.75 0 0 1 0 1.5H1.75A.75.75 0 0 1 1 2.75zm0 5A.75.75 0 0 1 1.75 7h12.5a.75.75 0 0 1 0 1.5H1.75A.75.75 0 0 1 1 7.75zm0 5a.75.75 0 0 1 .75-.75h12.5a.75.75 0 0 1 0 1.5H1.75a.75.75 0 0 1-.75-.75z"/></svg>`,
        action: () => Spicetify.Platform.History.push("/marketplace"),
      },
      {
        label: "Profile",
        icon: `<svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor"><path d="M8 1.5a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM3.5 4.5a4.5 4.5 0 1 1 9 0 4.5 4.5 0 0 1-9 0zM8 10c-3.037 0-5.5 1.343-5.5 3v1.5h11V13c0-1.657-2.463-3-5.5-3zm-7 3c0-2.761 3.134-4.5 7-4.5s7 1.739 7 4.5v2a.5.5 0 0 1-.5.5h-13a.5.5 0 0 1-.5-.5v-2z"/></svg>`,
        action: () => {
          const username =
            Spicetify.Platform.UserAPI?._product_state?.username ||
            Spicetify.Platform.username;
          if (username) {
            Spicetify.Platform.History.push(`/user/${username}`);
          }
        },
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
            "https://github.com/wktkow/clear-spotify-theme",
            "_blank",
          );
        },
      },
    ];

    items.forEach((item) => {
      const menuItem = document.createElement("button");
      menuItem.className = "clear-kebab-item";
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
})();
