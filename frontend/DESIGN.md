# UI decisions

Chronoscope is a desktop debugger workspace: session navigation, execution controls, one timeline, register/memory inspectors, and a paginated event list. Amber identifies historical inspection; green identifies the live process and verified restoration; fault states include explicit SIGSEGV text. Color supplements labels rather than replacing them.

The UI/UX skill's two design-system searches returned a documentation landing pattern, which is unsuitable. No generated system was persisted. General debugger layout judgment was used. The relevant dark developer-tool palette/monospace guidance and React state recommendations informed this interface. Local system fonts require no network resources. All displayed execution data comes from the C++ service.

Use native range and numeric inputs for keyboard timeline navigation, explicit Restore & replay action, visible focus, labeled controls, reduced-motion support, status/error announcements, 12-row event pagination, and responsive stacking at small widths. Historical browsing must never imply a restoration happened. Browser verification covers keyboard-accessible input, real replay, narrow layout and reduced motion.
