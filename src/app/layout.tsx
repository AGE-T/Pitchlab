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
  title: "PitchLab Research — Round 31: Telemetry & Latency-Truth Demo VST3",
  description:
    "Round 30 (final UNK closure, static continuation of the R26-R29 audit line): the last two standing unknowns closed with numbers — U-2: the host-facing getLatencySamples is vtable slot 6 (+0x30, NOT the long-hunted +0x88) at 0x18000E540, returning max(0, [VoxAudioProcessor+0x3C]), set by the 'live'-mode prepare via the JUCE-shape setLatencySamples 0x180260220 to flag ? 8 : (int)(sampleRate·0.04 + 0.5) + 8 (40 ms pitch-shifter window + 8 samples of pipeline overhead; 48 kHz → 1928), with the processor class named by RTTI as VoxAudioProcessor (tide::DigiPluginBase → tide::ExtendedAudioProcessor → tide::JuceAdapter → juce::AudioProcessor) and the JUCE VST3ClientExtensions dynamic_cast proven dead code (zero deriving classes — the R29 aggregation-path verdict withdrawn); U-3: the m02 ids-12/13 feed is the per-voice block-rate poller 0x1800E5160 calling modpp::simple::env_t::get_child_float (vtable 0x18040AE10 slot 16, +0x80) with the id IMMEDIATE in r8d — the fifth dispatch shape no earlier census scanned — which dispatches the voice's engine (vtable 0x180409F60, per-voice at voice+0x430) m02 case 12/13 (∓cents telemetry) and fans results out as atomic dirty bits — the binary was never executed (SHA-256 unchanged).",
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
