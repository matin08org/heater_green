using System;
using System.Drawing;
using System.Drawing.Imaging;
public static class HmiPanelRaster {
 private static readonly int[,] Bayer={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
 // Supersampled semantic drawing -> two 2-bit roles + 4-bit coverage.
 public static Bitmap Pack(Bitmap source,int x,int y,int width,int height,int scale) {
  Bitmap result=new Bitmap(width,height,PixelFormat.Format32bppArgb);
  int[] counts=new int[4];
  for(int py=0;py<height;py++)for(int px=0;px<width;px++) {
   Array.Clear(counts,0,4);
   for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++) {
    Color c=source.GetPixel((x+px)*scale+sx,(y+py)*scale+sy);
    int role=(c.R+42)/85;if(role>3)role=3;counts[role]++;
   }
   int first=0;for(int k=1;k<4;k++)if(counts[k]>counts[first])first=k;
   int second=first==0?1:0;for(int k=0;k<4;k++)if(k!=first&&counts[k]>counts[second])second=k;
   int alpha=15;
   if(counts[second]>0)alpha=(15*counts[first]+(counts[first]+counts[second])/2)/(counts[first]+counts[second]);
   else second=first;
   // Restrained glass shading of background uses only the panel's own palette.
   if(first==0&&counts[0]==scale*scale&&height>=25) {
    double glow=(1.0-(double)py/Math.Max(1,height-1))*(1.0-0.4*(double)px/Math.Max(1,width-1));
    double level=glow*2.0;int low=(int)Math.Floor(level);
    first=1;second=0;alpha=low+((level-low)>(Bayer[py&3,px&3]+0.5)/16.0?1:0);
   }
   result.SetPixel(px,py,Color.FromArgb(255,first*85,second*85,alpha*17));
  }
  return result;
 }
}
