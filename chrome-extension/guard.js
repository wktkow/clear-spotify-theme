/**
 * Clear Theme – Chrome Extension Guard
 *
 * Runs before theme.js at document_start. Handles:
 *   1. Toggling the theme CSS when disabled (CSS is injected by Chrome
 *      via manifest content_scripts.css, bypassing CSP).
 *   2. Skipping the splash screen in the Chrome extension.
 *   3. Login guard banner for logged-out users.
 *
 * This file is ONLY included in the Chrome extension build.
 */
(function () {
  "use strict";

  const DISABLED_KEY = "clear-extension-disabled";

  /* ── Class bridge ─────────────────────────────────────────────────
   * The web player doesn't have Spicetify's Root__* or main-* class
   * names.  We inject them onto the matching web player elements so
   * our existing CSS selectors work without modification.
   * Uses stable identifiers: IDs, data-testid, aria-labels.
   * ─────────────────────────────────────────────────────────────── */

  console.log("[Clear Theme] guard.js v7-bridge loaded");

  /**
   * Mapping of CSS selector → Spicetify class(es) to add.
   * Selectors use only stable web player identifiers.
   */
  const CLASS_MAP = [
    // Layout panels
    { sel: "#main-view", cls: ["Root__main-view"] },
    { sel: "#Desktop_LeftSidebar_Id", cls: ["Root__nav-bar"] },
    { sel: "[data-right-sidebar-hidden]", cls: ["Root__top-container"] },

    // Search section: web uses hashed class names, desktop uses main-globalNav-*.
    // These mappings keep the search bar visible in the dynamic nav CSS.
    {
      sel: '#global-nav-bar > div:has([role="search"])',
      cls: ["main-globalNav-searchSection"],
    },
    {
      sel: '#global-nav-bar div:has(> [data-testid="home-button"])',
      cls: ["main-globalNav-searchContainer"],
    },
    {
      sel: '#global-nav-bar [role="search"]',
      cls: ["main-globalNav-searchInputContainer"],
    },

    // Now-playing bar (web uses <aside>, desktop uses <footer>)
    {
      sel: '[data-testid="now-playing-bar"]',
      cls: ["Root__now-playing-bar", "main-nowPlayingBar-container"],
    },

    // Now-playing bar inner div (first child of the aside)
    {
      sel: '[data-testid="now-playing-bar"] > div',
      cls: ["main-nowPlayingBar-nowPlayingBar"],
    },

    // Global navigation (root already has "Root global-nav")
    { sel: '[data-testid="root"]', cls: ["Root__top-container"] },
  ];

  /**
   * Find the right sidebar grid child and inject Root__right-sidebar.
   * Walks up from Desktop_PanelContainer_Id to the direct child of the
   * grid container ([data-right-sidebar-hidden]).  This element has
   * grid-area: right-sidebar in Spotify's CSS and needs the Spicetify
   * class for the sidebar-swap snippet to move it to left-sidebar.
   */
  function injectRightSidebar() {
    if (document.querySelector(".Root__right-sidebar")) return 0;
    const grid = document.querySelector("[data-right-sidebar-hidden]");
    if (!grid) return 0;
    const panel = document.getElementById("Desktop_PanelContainer_Id");
    if (!panel) return 0;
    let el = panel;
    while (el && el.parentElement !== grid) {
      el = el.parentElement;
    }
    if (el && el.parentElement === grid) {
      el.classList.add("Root__right-sidebar");
      return 1;
    }
    return 0;
  }

  function injectClasses() {
    let injected = 0;
    for (const { sel, cls } of CLASS_MAP) {
      const els = document.querySelectorAll(sel);
      for (const el of els) {
        for (const c of cls) {
          if (!el.classList.contains(c)) {
            el.classList.add(c);
            injected++;
          }
        }
      }
    }
    injected += injectRightSidebar();
    return injected;
  }

  // Run immediately + on DOM ready + with observer for SPA navigation
  function startClassBridge() {
    injectClasses();

    // Re-inject after Spotify's React hydration
    if (document.body) {
      new MutationObserver(() => injectClasses()).observe(document.body, {
        childList: true,
        subtree: true,
      });
    } else {
      document.addEventListener(
        "DOMContentLoaded",
        () => {
          injectClasses();
          new MutationObserver(() => injectClasses()).observe(document.body, {
            childList: true,
            subtree: true,
          });
        },
        { once: true },
      );
    }

    // Safety net: re-inject a few times during load
    setTimeout(injectClasses, 1000);
    setTimeout(injectClasses, 3000);
    setTimeout(injectClasses, 6000);
  }

  startClassBridge();

  /* ── CSS toggle ───────────────────────────────────────────────────── */

  /**
   * Toggle our theme CSS. Chrome injects manifest CSS as <style> elements.
   * We find ours by checking for the --clear-ext-loaded marker in the rules,
   * or by checking for a chrome-extension:// href.
   */
  function setThemeSheetsDisabled(disabled) {
    let found = 0;
    for (const sheet of document.styleSheets) {
      try {
        // Check href for chrome-extension://
        if (sheet.href && sheet.href.includes("chrome-extension://")) {
          sheet.disabled = disabled;
          found++;
          continue;
        }
        // Check inline styles for our marker
        if (!sheet.href && sheet.cssRules) {
          for (let i = 0; i < Math.min(sheet.cssRules.length, 5); i++) {
            if (
              sheet.cssRules[i].cssText &&
              sheet.cssRules[i].cssText.includes("--clear-ext-loaded")
            ) {
              sheet.disabled = disabled;
              found++;
              break;
            }
          }
        }
      } catch (e) {
        /* cross-origin sheet, ignore */
      }
    }
    return found;
  }

  /* ── Disabled state ───────────────────────────────────────────────── */

  // If user previously clicked "Disable", turn off the CSS until logged in.
  if (localStorage.getItem(DISABLED_KEY) === "true") {
    window.__clearExtensionDisabled = true;

    // Aggressively disable sheets — they may not be in styleSheets yet
    const tryDisable = () => setThemeSheetsDisabled(true);
    tryDisable();
    const iv = setInterval(() => {
      if (tryDisable() >= 2) clearInterval(iv);
    }, 50);
    setTimeout(() => clearInterval(iv), 5000);

    document.addEventListener("DOMContentLoaded", tryDisable, { once: true });
    window.addEventListener("load", tryDisable, { once: true });

    // Re-enable automatically on login
    const recheck = setInterval(() => {
      tryDisable();
      if (isLoggedIn()) {
        localStorage.removeItem(DISABLED_KEY);
        window.__clearExtensionDisabled = false;
        clearInterval(recheck);
        window.location.reload();
      }
    }, 2000);

    return; // theme.js will also exit early
  }

  /* ── Splash skip ──────────────────────────────────────────────────── */

  // The splash screen is a desktop-only UX feature. Skip it entirely
  // in the Chrome extension so the page is never covered by a black overlay.
  window.__clearExtensionNoSplash = true;
  function applyNoSplash() {
    if (document.body) {
      document.body.classList.add("clear-no-splash");
    } else {
      document.addEventListener(
        "DOMContentLoaded",
        () => document.body.classList.add("clear-no-splash"),
        { once: true },
      );
    }
  }
  applyNoSplash();

  /* ── Login guard ──────────────────────────────────────────────────── */

  function isLoggedIn() {
    // Logged-in Spotify has a user widget; logged-out pages show login buttons
    if (
      document.querySelector(
        '[data-testid="user-widget-link"], [data-testid="user-widget"]',
      )
    ) {
      return true;
    }
    // Login/signup page
    if (
      window.location.pathname.startsWith("/login") ||
      window.location.pathname.startsWith("/signup")
    ) {
      return false;
    }
    // If there's a login button visible, user is not logged in
    if (
      document.querySelector(
        '[data-testid="login-button"], [data-testid="signup-button"]',
      )
    ) {
      return false;
    }
    // Default: assume logged in (don't block on unknown pages)
    return true;
  }

  function showBanner() {
    // Don't double-create
    if (document.getElementById("clear-guard-banner")) return;

    const banner = document.createElement("div");
    banner.id = "clear-guard-banner";
    banner.style.cssText = [
      "position: fixed",
      "bottom: 1rem",
      "left: 50%",
      "transform: translateX(-50%)",
      "z-index: 999999",
      "background: #181818",
      "color: #fff",
      "border: 1px solid #333",
      "border-radius: 0.5rem",
      "padding: 0.875rem 1.25rem",
      "font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif",
      "font-size: 0.8125rem",
      "line-height: 1.5",
      "max-width: 28rem",
      "text-align: center",
      "box-shadow: 0 4px 24px rgba(0,0,0,0.5)",
    ].join(";");

    const message = document.createElement("p");
    message.style.cssText = "margin: 0 0 0.625rem 0";
    message.textContent =
      "Clear Theme is active but you're not logged in. If you're having trouble logging in or see visual bugs, you can disable the theme until you sign in.";

    const btn = document.createElement("button");
    btn.textContent = "Disable until login";
    btn.style.cssText = [
      "background: #fff",
      "color: #000",
      "border: none",
      "border-radius: 9999px",
      "padding: 0.5rem 1.25rem",
      "font-size: 0.8125rem",
      "font-weight: 600",
      "cursor: pointer",
    ].join(";");
    btn.addEventListener("mouseenter", () => {
      btn.style.background = "#e0e0e0";
    });
    btn.addEventListener("mouseleave", () => {
      btn.style.background = "#fff";
    });
    btn.addEventListener("click", () => {
      localStorage.setItem(DISABLED_KEY, "true");
      window.location.reload();
    });

    banner.appendChild(message);
    banner.appendChild(btn);
    document.body.appendChild(banner);
  }

  function removeBanner() {
    const el = document.getElementById("clear-guard-banner");
    if (el) el.remove();
  }

  // Wait for the page to have enough DOM to check login state
  function init() {
    if (!document.body) {
      document.addEventListener("DOMContentLoaded", init, { once: true });
      return;
    }

    // Initial check after a short delay for Spotify's SPA to hydrate
    setTimeout(() => {
      if (!isLoggedIn()) {
        showBanner();
      }

      // Keep watching – Spotify is a SPA so login state can change
      new MutationObserver(() => {
        if (isLoggedIn()) {
          removeBanner();
          localStorage.removeItem(DISABLED_KEY);
        } else if (!document.getElementById("clear-guard-banner")) {
          showBanner();
        }
      }).observe(document.body, { childList: true, subtree: true });
    }, 1500);
  }

  init();
})();
