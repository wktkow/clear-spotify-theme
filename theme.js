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
