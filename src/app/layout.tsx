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
  title: "PitchLab Research — Round 27: Residual UNK Closure (Static)",
  description:
    "Round 27 (residual UNK closure, user-directed static continuation of the R26 audit): the LIVE pan law recovered — LINEAR 0.5·(1∓p)·G with G = 10^(gain_dB/20) in the stereomixer3 wrapper update (the R26 trigonometric verdict described real but unreachable framework-library code); the mix audio runs on a framework-wide 4-sample cadence with double-precision index-order accumulation and one-pole-smoothed gains; the softclip is a tanh mean-value clipper (ln-cosh divided difference + tanh midpoint, DC transfer b·tanh(a·x)); Live-Mode 40 = 40 ms with the conversion site traced (int(sr·ms·0.001+0.5), cap 1921532); the hungarian scale choice is index-clamped to the neapolitan row (no out-of-bounds ever); the m01 id0 field has a reader (the correlate scan-base at 0x18013C868); m02 is reached through an event-router subscriber multicast (event-in vtable slots 2/3); and the 256×6 kernel is numerically pinned as a fractional-delay bank (DC group delay = 2 + r/256 exactly, mixed phase) — the binary was never executed (SHA-256 unchanged).",
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
