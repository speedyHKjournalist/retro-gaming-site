"use strict";

// Google AdSense in the side rails (.ad-rail, 120 px wide). The loader
// (adsbygoogle.js) is in each page's <head>; this puts one display ad unit in
// every rail on screen. RAIL_AD_SLOT is the data-ad-slot of a display ad unit
// made in AdSense (Ads > By ad unit > Display ads, fixed size 120 x 600):
// until it is set, the rails keep their "Reserved" placeholder.
(function() {
    const AD_CLIENT = "ca-pub-8341075370183667";
    const RAIL_AD_SLOT = "";

    if (!RAIL_AD_SLOT) return;

    // A rail is hidden on narrow screens: it gets its ad when it is shown,
    // so an ad is never requested for a rail that is not on screen.
    function fillRails() {
        for (const rail of document.querySelectorAll(".ad-rail:not(.has-ad)")) {
            if (!rail.offsetWidth) continue;
            const label = document.createElement("span");
            label.textContent = "Advertisement";
            const unit = document.createElement("ins");
            unit.className = "adsbygoogle";
            unit.dataset.adClient = AD_CLIENT;
            unit.dataset.adSlot = RAIL_AD_SLOT;
            rail.replaceChildren(label, unit);
            rail.classList.add("has-ad");
            (window.adsbygoogle = window.adsbygoogle || []).push({});
        }
    }

    fillRails();
    let resizeTimer = 0;
    window.addEventListener("resize", function() {
        clearTimeout(resizeTimer);
        resizeTimer = setTimeout(fillRails, 250);
    });
})();
