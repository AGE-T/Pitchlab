import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import "./globals.css";
import { Toaster } from "@/components/ui/toaster";

const geistSans = Geist({
  variable: "--font-geist-sans",
  subsets: ["latin"],
});

const geistMono = Geist_Mono({
  variable: "--font-geist-mono",
  subsets: ["latin"],
});

export const metadata: Metadata = {
  title: "PitchLab Research — Round 28: Event/Query Protocol & Parameter Graph (Static)",
  description:
    "Round 28 (event/query protocol & parameter graph, static continuation of the R26/R27 audit line): the event system reclassified — the hierarchical publisher/router is the DebugReport section-tree OBSERVER system, not the DSP event feed (R27's classification corrected); the DSP runs a module query protocol with container demux thunks and the engine's m01/m02 as the only custom handlers; m02 fully decoded (ids 7..13: cents from C4 with the −65535 sentinel, frequency ratio, Q23-quantised ratio, sqrt(frac·(1−2⁻²³)), a confidence-like combination, and the ∓cents corrections of engine+0xA58C/0xA588 gated by a dB-domain test); userscale closed as a UI-legacy parameter (registered + applier-wired write side, zero readers — the map hypothesis refuted); c_minmax closed as a patch-arity min/max aggregator with delay-family units of milliseconds on the int16 scale, a Q23 fixed-point deglitch ramp (step 2²³/N) and a 5-tap read neighbourhood; and the host-latency false lead eliminated (the getLatencySamples=0-stub claim withdrawn; the diagnostic attribute serialises a never-written field) — the binary was never executed (SHA-256 unchanged).",
  keywords: ["Octavox", "Eventide", "reverse engineering", "REA", "Ghidra", "DSP", "pitch shifting", "minimum pitch"],
  authors: [{ name: "Z.ai Team" }],
  icons: {
    icon: "https://z-cdn.chatglm.cn/z-ai/static/logo.svg",
  },
  openGraph: {
    title: "Z.ai Code Scaffold",
    description: "AI-powered development with modern React stack",
    url: "https://chat.z.ai",
    siteName: "Z.ai",
    type: "website",
  },
  twitter: {
    card: "summary_large_image",
    title: "Z.ai Code Scaffold",
    description: "AI-powered development with modern React stack",
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" suppressHydrationWarning>
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased bg-background text-foreground`}
      >
        {children}
        <Toaster />
      </body>
    </html>
  );
}
