/**
 * Clear Theme – Chrome Extension Login Guard
 *
 * Runs before theme.js at document_start. Handles two things:
 *   1. Conditionally injects the theme CSS (colors.css + user.css).
 *      CSS is NOT declared in the manifest so we can skip it when disabled.
 *   2. Checks whether the user is logged into Spotify. If not, shows a
 *      persistent banner with a "Disable until login" button.
 *
 * This file is ONLY included in the Chrome extension build.
 */
(function () {
  "use strict";

  const DISABLED_KEY = "clear-extension-disabled";

  /* ── CSS injection ────────────────────────────────────────────────── */

  const TAG = "[Clear Theme]";

  function log(...args) {
    console.log(TAG, ...args);
  }

  function warn(...args) {
    console.warn(TAG, ...args);
  }

  /**
   * Create <style> elements from the embedded CSS data and append them to
   * the given target (document.head or document.documentElement).
   * Returns the array of created <style> nodes.
   */
  function createStyleElements(cssData, target) {
    const styles = [];
    for (const [name, css] of Object.entries(cssData)) {
      const style = document.createElement("style");
      style.setAttribute("data-clear-theme", name);
      style.textContent = css;
      target.appendChild(style);
      styles.push(style);
      log(`Injected <style data-clear-theme="${name}"> (${css.length} chars) → ${target.nodeName}`);
    }
    return styles;
  }

  /**
   * Move our <style> tags to the very end of <head> so they come AFTER
   * all of Spotify's <link> stylesheets in the cascade.
   */
  function promoteStyles() {
    const head = document.head;
    if (!head) {
      warn("promoteStyles: <head> not available yet");
      return;
    }
    const ours = head.querySelectorAll("style[data-clear-theme]");
    if (ours.length === 0) {
      warn("promoteStyles: no style[data-clear-theme] found in <head>!");
      return;
    }
    // Check if they're already last
    const allChildren = [...head.children];
    const lastChild = allChildren[allChildren.length - 1];
    if (lastChild && lastChild.hasAttribute && lastChild.hasAttribute("data-clear-theme")) {
      return; // already at the end
    }
    ours.forEach((s) => head.appendChild(s));
    log(`promoteStyles: moved ${ours.length} style(s) to end of <head> (${head.children.length} children total)`);
  }

  function injectThemeCSS() {
    const cssData = window.__clearThemeCSS;
    log("injectThemeCSS called");
    log("  window.__clearThemeCSS exists:", !!cssData);
    log("  typeof:", typeof cssData);

    if (!cssData || typeof cssData !== "object") {
      warn("CSS data missing! css-data.js may not have run before guard.js");
      return;
    }

    const keys = Object.keys(cssData);
    log("  keys:", keys.join(", "));
    for (const k of keys) {
      log(`  ${k}: ${cssData[k].length} chars, starts with: "${cssData[k].substring(0, 60)}..."`);
    }

    // Phase 1: Inject immediately into whatever target is available
    const target = document.head || document.documentElement;
    log("Phase 1: immediate inject →", target.nodeName);
    createStyleElements(cssData, target);

    // Phase 2: On DOMContentLoaded, ensure styles are in <head> at the end
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", () => {
        log("Phase 2: DOMContentLoaded fired, readyState =", document.readyState);

        // If we injected into <html> before <head> existed, move to <head>
        const orphans = document.documentElement.querySelectorAll(
          ":scope > style[data-clear-theme]"
        );
        if (orphans.length > 0 && document.head) {
          log(`  Moving ${orphans.length} orphan style(s) from <html> to <head>`);
          orphans.forEach((s) => document.head.appendChild(s));
        }

        promoteStyles();
      }, { once: true });
    } else {
      log("Phase 2: already past loading, promoting now");
      promoteStyles();
    }

    // Phase 3: After window load, promote again (Spotify lazy-loads CSS)
    window.addEventListener("load", () => {
      log("Phase 3: window.load fired");
      promoteStyles();
    }, { once: true });

    // Phase 4: Periodic promotion at 2s, 5s, 10s to catch late-loaded CSS
    for (const delay of [2000, 5000, 10000]) {
      setTimeout(() => {
        log(`Phase 4: ${delay}ms timer fired`);
        promoteStyles();

        // Verify our styles are actually in the DOM
        const found = document.querySelectorAll("style[data-clear-theme]");
        log(`  Found ${found.length} style[data-clear-theme] in DOM`);
        found.forEach((s) => {
          log(`    ${s.getAttribute("data-clear-theme")}: ${s.textContent.length} chars, parent: ${s.parentNode?.nodeName}`);
        });

        // Nuclear: if styles are somehow gone, re-inject
        if (found.length === 0) {
          warn(`  STYLES DISAPPEARED! Re-injecting from cssData`);
          createStyleElements(cssData, document.head || document.documentElement);
          promoteStyles();
        }
      }, delay);
    }

    // Phase 5: MutationObserver — whenever Spotify adds a new <link>, re-promote
    function watchHead() {
      const head = document.head;
      if (!head) {
        log("Phase 5: <head> not ready, waiting...");
        const obs = new MutationObserver(() => {
          if (document.head) {
            obs.disconnect();
            watchHead();
          }
        });
        obs.observe(document.documentElement, { childList: true });
        return;
      }
      log("Phase 5: watching <head> for new stylesheets");
      new MutationObserver((mutations) => {
        const hasNewSheet = mutations.some((m) =>
          [...m.addedNodes].some(
            (n) => n.nodeName === "LINK" && (n.rel === "stylesheet" || n.type === "text/css"),
          ),
        );
        if (hasNewSheet) {
          log("Phase 5: new <link> stylesheet detected, promoting");
          promoteStyles();
        }
      }).observe(head, { childList: true });
    }
    watchHead();

    log("injectThemeCSS complete — all phases scheduled");
  }

  /* ── Disabled state ───────────────────────────────────────────────── */

  // If user previously clicked "Disable", block CSS + JS until logged in.
  // Once they log in, the flag is automatically cleared.
  if (localStorage.getItem(DISABLED_KEY) === "true") {
    window.__clearExtensionDisabled = true;

    // Keep checking – once they log in, re-enable automatically
    const recheck = setInterval(() => {
      if (isLoggedIn()) {
        localStorage.removeItem(DISABLED_KEY);
        window.__clearExtensionDisabled = false;
        clearInterval(recheck);
        window.location.reload();
      }
    }, 2000);

    return; // No CSS injected, theme.js will also exit early
  }

  // Not disabled → inject theme styles immediately (before DOM paints)
  injectThemeCSS();

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
