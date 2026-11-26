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
  title: "PitchLab Research — Round 29: Host COM Surface & Non-Immediate Id Census (Static)",
  description:
    "Round 29 (host COM surface & non-immediate id census, static continuation of the R26-R28 audit line): the U-2 COM-vtable hunt executed to the last hop — the true export table pinned (GetPluginFactory at 0x180194440; the old mapping was the ExitDll stub), the JUCE factory chain decoded with runtime-generated class IDs from the string Octavox (no static class array), the processor class identified by RTTI as juce::JuceVST3Component (a 0x480-byte broker with six vtable views, the 0x28A0 Eventide engine aggregated at +0x38, a dual-path queryInterface with RTTI-walk aggregation first and a 7-IID fallback whose IAudioProcessor answer is this+8), and the fallback surface proven structurally non-viable for latency (its +0x88 would return a vtable pointer) — the host-facing IAudioProcessor is served by the aggregation path (one-hop residual); the U-3 immediate surface exhausted across all four dispatch slot shapes including the demux argument shift the R28 scan missed (every immediate id in 7..13 is a writer length, stream-read length or format parameter — no genuine id-12/13 feed exists; a 1337-site non-immediate census delivered) — the binary was never executed (SHA-256 unchanged).",
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
