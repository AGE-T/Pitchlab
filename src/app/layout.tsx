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
  title: "PitchLab Research — Round 33: The Minimal Editor (VST3 GUI)",
  description:
    "Round 33 (the minimal editor, on the r31-telemetry-demo branch): the PitchLab wrapper's first GUI surface, delivered on the user's request — a fixed-size 784x416 software-rendered IPlugView served by createView(\"Editor\"): all 38 parameters reachable (global pitch/voices/harmony-map/feedback/mix + per-voice transpose/gain/delay/mute + the clickable bypass LED) with every edit on the exact quantized grid through the canonical beginEdit/performEdit/endEdit protocol, double-click = documented default; the R31 IPitchTelemetry vendor telemetry displayed live (per-voice Hz, cents, confidence bars, glide ratios, dirty-bit LEDs; the -65535 sentinel and zeroed cells render as no-data). No VSTGUI and no font stack: an embedded classic 5x7 bitmap font, core Xlib on Linux / user32+gdi32 on Windows; one 0x00RRGGBB pixel format serves both the X11 ZPixmap and the Win32 DIB blits. Linux runs the view on the host's thread via the VST 3.6.8+ IRunLoop contract (no private thread); Windows is a WS_CHILD + DIB on the host thread, cross-built with the R32 Zig 0.13.0 pipeline and machine-code verified (GDI32 new import, zero X11 references, ymm=275 vfmadd=26 identical to R32-W; macOS honestly not implemented). Verification: core 115/115 Release+ASan (r15acc hash unchanged — core-neutral round), wrapper 21/21, NEW ui tests 11/11 Release+ASan including the X11 PIXEL-IDENTITY proof (server-side pixels memcmp-identical to the render buffer under a real Xvfb server) and a golden full-frame render pin (FNV-1a 0x0f80c7f43a8358ad), module integration 17/17 (the new 17th case drives the shipped module's view: dlopen, factory, createView, real attach, painted-pixel probe; the pre-existing IPtr double-own leak the LSan gate exposed is fixed), Steinberg validator 47/47. Delivery: PitchLab_R33_Minimal_UI_VST3.zip (Linux + Windows bundles + PDB + README + report + verification logs + the two real-render screenshots).",
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
