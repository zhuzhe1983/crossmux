"""Compare the actual firmware icon renderer against preprocessed PNG assets."""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageOps

ROOT=Path(__file__).resolve().parents[2]

def main():
    with tempfile.TemporaryDirectory(prefix='app-icons-') as directory:
        work=Path(directory)
        (work/'components/themes').mkdir(parents=True)
        names=['None','Library','Transfer','Opds','WeRead','AirPage','ReadingStats','Sudoku','Gomoku','Sokoban','ChineseChess','Minesweeper','Game2048','Avatar','Buddy','PixelSwitch','Calculator','Woodfish','Standby']
        (work/'components/themes/BaseTheme.h').write_text('#pragma once\nenum UIIcon {'+','.join(names)+'};\n')
        (work/'BoardConfig.h').write_text('#pragma once\nnamespace BoardConfig {struct Profile {int grayscaleLevels=16;};inline constexpr Profile DEFAULT_DEVICE{};}\n')
        (work/'GfxRenderer.h').write_text(r'''#pragma once
#include <cstdint>
#include <array>
class GfxRenderer {
 public:
 enum RenderMode {BW,GRAYSCALE_LSB,GRAYSCALE_MSB};
 struct Pixel {bool draw,state;};
 mutable std::array<uint8_t,96*96> image{};
 bool native=false; RenderMode mode=BW; int strip=-1;
 bool glyphIntersectsStrip(int x0,int,int x1,int)const{return strip<0 || (x0<=strip && x1>strip);}
 bool isGrayscale16Active()const{return native;}
 RenderMode getRenderMode()const{return mode;}
 void drawPixel(int x,int y,bool black)const{if(strip>=0 && x!=strip)return;image[y*96+x]=black?0:255;}
 void drawGrayscale16Pixel(int x,int y,uint8_t value)const{image[y*96+x]=value;}
 static Pixel mapTwoBitPixel(RenderMode mode,uint8_t value){return {mode==GRAYSCALE_LSB?value==1:(value==1||value==2),false};}
};
''')
        (work/'test.cpp').write_text(r'''
#include "SemanticAppIcons.h"
#include <fstream>
#include <cstdlib>
int main(int argc,char**argv){
 if(argc!=5)return 1;
 const int levels=std::atoi(argv[1]),size=std::atoi(argv[2]);const bool invert=std::atoi(argv[3]);
 GfxRenderer renderer;renderer.native=levels==16;
 SemanticAppIcons::draw(renderer,UIIcon::Library,0,0,size,levels,invert);
 auto bw=renderer.image;
 if(levels==4){
   renderer.image.fill(0);renderer.mode=GfxRenderer::GRAYSCALE_LSB;
   for(int x=0;x<size;++x){renderer.strip=x;SemanticAppIcons::draw(renderer,UIIcon::Library,0,0,size,levels,invert);}
   auto lsb=renderer.image;renderer.image.fill(0);renderer.mode=GfxRenderer::GRAYSCALE_MSB;
   for(int x=0;x<size;++x){renderer.strip=x;SemanticAppIcons::draw(renderer,UIIcon::Library,0,0,size,levels,invert);}
   for(int i=0;i<96*96;++i)bw[i]=lsb[i]?85:renderer.image[i]?170:bw[i];
 }else bw=renderer.image;
 std::ofstream file(argv[4],std::ios::binary);for(int y=0;y<size;++y)file.write(reinterpret_cast<char*>(bw.data()+y*96),size);
}
''')
        binary=work/'test'
        subprocess.run(['c++','-std=c++17','-fno-exceptions','-DENABLE_CHINESE_VERSION','-I'+str(work),'-I'+str(ROOT/'src/components/icons'),str(work/'test.cpp'),str(ROOT/'src/components/icons/SemanticAppIcons.cpp'),'-o',str(binary)],check=True)
        count=0
        for levels,sizes in [(2,(32,64)),(4,(32,64)),(16,(32,64,96))]:
            for size in sizes:
                source_size=96 if levels==16 and size>32 else size
                mode='dither' if levels<16 and size>=64 else 'plain'
                path=ROOT/f'assets/app-icons/semantic-v1/g{levels}/{source_size}/{mode}/library.png'
                # Firmware uses nearest-neighbor floor sampling, not Pillow's centered kernel.
                with Image.open(path) as image:
                    pixels=image.convert('L')
                    expected=bytes(pixels.getpixel((x*source_size//size,y*source_size//size)) for y in range(size) for x in range(size))
                for inverse in (False,True):
                    output=work/'out.raw'
                    subprocess.run([str(binary),str(levels),str(size),str(int(inverse)),str(output)],check=True)
                    assert output.read_bytes()==(bytes(255-v for v in expected) if inverse else expected),(levels,size,inverse)
                    count+=1
        print(f'{count} actual icon-renderer native/BW/strip/inversion comparisons passed')

if __name__=='__main__':main()
