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
  title: "Octavox Research — Round 24 Findings",
  description:
    "Round 24 (SIMD capability layer): the YIN difference-function and window-peak kernels in per-TU ISA units (AVX2 / AVX2+FMA), the order-preserving lag-axis AVX2 kernel bit-identical to the scalar reference (4.8-6.3x kernel, FullRate 96 kHz det_estimate 5.9x, 8-voice stereo wall 1144 -> 368 ms/s), Config::simd with the bit-exact Scalar default, the 110/110 suite (Release + ASan/UBSan) with the R24 bit-exactness battery, the rebuilt VST3 chain (21/21 + 12/12 + 47/47) and the measured decision record — Eventide Octavox reverse engineering, round 24.",
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
