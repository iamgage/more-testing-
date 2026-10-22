# Quest UI redesign (experimental source)

- Replaced horizontal tab bar with a simple home page of three vertically stacked navigation buttons.
- Each page shows only its own controls, plus Back.
- Channel keyboard has six one-character buttons per page, with Next/Previous, Backspace, and Done. This trades speed for avoiding BSML Lite button-width collisions on Quest.
- Play Song click is deferred through the BSML scheduler, but the navigation implementation itself is unchanged and remains unverified.
- Twitch/BSR/networking and the currently rendering chat overlay are preserved.
- Not compiled or tested on Quest.
