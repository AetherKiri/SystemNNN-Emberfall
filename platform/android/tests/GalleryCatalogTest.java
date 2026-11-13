package com.tyranor.runtime.systemnnn;

import java.io.File;
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/** The original CG token grammar and logical sequence require no bitmap or JNI. */
public final class GalleryCatalogTest {
    private static int checks;
    private static void check(boolean value,String message){
        checks++;
        if(!value)throw new AssertionError(message);
    }
    private static int cgKey(int group,int index){
        return (1<<28)|((group&0xfff)<<16)|(index&0xffff);
    }
    private static void writeText(Path file,String text)throws IOException{
        Files.createDirectories(file.getParent());
        Files.write(file,text.getBytes(StandardCharsets.UTF_8));
    }
    private static void deleteTree(Path root)throws IOException{
        if(root==null||!Files.exists(root))return;
        try(java.util.stream.Stream<Path> walk=Files.walk(root)){
            for(Path p:walk.sorted(Comparator.reverseOrder()).toArray(Path[]::new))Files.deleteIfExists(p);
        }
    }
    private static void checkDifferenceCatalogAndFont()throws Exception{
        List<String> rows=Arrays.asList("@orphan0","base1_a","@base1_b","*@base1_c","single2","base3_a","@base3_b");
        int[] keys={cgKey(2,1),cgKey(2,3),cgKey(2,5),cgKey(1,6)};
        int[][] counts=GalleryCatalog.blockVariantCounts(rows,keys,2);
        check(Arrays.deepEquals(counts,new int[][]{{0,1},{2,3},{0,1},{1,2}}),
            "CG difference blocks group @ rows and count unlocked variants per group");

        check(!GalleryCatalog.differenceVisible(0,1,2,3)&&!GalleryCatalog.differenceVisible(2,1,2,3),
            "differencePrintFlag bit 0 gates CG difference counter");
        check(!GalleryCatalog.differenceVisible(1,0,0,3)&&GalleryCatalog.differenceVisible(1,1,0,3),
            "differencePrintByGetFlag controls 0/total visibility on locked multi-variant blocks");
        check(!GalleryCatalog.differenceVisible(1,1,1,1)&&GalleryCatalog.differenceVisible(3,0,1,1)&&
            !GalleryCatalog.differenceVisible(3,0,0,1)&&!GalleryCatalog.differenceVisible(3,1,0,0),
            "differencePrintFlag bit 1 enables 1/1 only when unlocked or byGet permits");

        check(Arrays.deepEquals(GalleryCatalog.differenceGlyphs(3,5,2,21),new int[][]{{63,5},{42,11},{21,3}}),
            "CSuuji single-digit total shifts putX4 left by fontNextX and suppresses leading zero");
        check(Arrays.deepEquals(GalleryCatalog.differenceGlyphs(10,12,2,16),
            new int[][]{{64,2},{48,1},{32,11},{16,0},{0,1}}),
            "CSuuji two-digit got/total emits ones-to-tens digits around slash glyph 11");
        check(Arrays.deepEquals(GalleryCatalog.differenceGlyphs(0,4,2,10),new int[][]{{30,4},{20,11},{10,0}}),
            "CSuuji zero unlocked count prints single 0 glyph");

        Path temp=Files.createTempDirectory("systemnnn-gallery-test");
        try{
            GameTextDecoder codec=new GameTextDecoder(new byte[0],null);
            GameUiAssets.Config defaults=GameUiAssets.readConfig(temp.toFile(),"selectcg",codec);
            check(GalleryCatalog.differenceFont(defaults).equals("sys/ta_sl_font3"),
                "omitted fileNameDifferenceFont preloads upstream default sys/ta_sl_font3");

            writeText(temp.resolve("init/selectcg.xtx"),
                "differencePrintFlag 3\nfileNameDifferenceFont ta_diff_num.DWQ\n");
            GameUiAssets.Config custom=GameUiAssets.readConfig(temp.toFile(),"selectcg",codec);
            check(GalleryCatalog.differenceFont(custom).equals("sys/ta_diff_num")&&
                custom.imageNames().contains("sys/ta_diff_num"),
                "custom fileNameDifferenceFont normalizes to sys/ key and enters imageNames");

            writeText(temp.resolve("init/selectcg.xtx"),
                "differencePrintFlag 2\nfileNameDifferenceFont ta_diff_num\n");
            GameUiAssets.Config disabled=GameUiAssets.readConfig(temp.toFile(),"selectcg",codec);
            check(GalleryCatalog.differenceFont(disabled).isEmpty(),
                "differencePrintFlag without bit 0 skips difference font preload");

            writeText(temp.resolve("init/datafile.xtx"),
                "selectPrintMode 2\n"
                +"fileNameSelectPic sel_mode1\n"
                +"fileNameSaveCursor cur_save\n"
                +"fileNameLoadCursor cur_load\n"
                +"fileNameOverwriteCursor cur_over\n"
                +"saveAnimePattern 3\nsaveAnimeType 1\nsaveAnimeSpeed 4\n"
                +"overwriteAnimePattern 4\noverwriteAnimeType 7\noverwriteAnimeSpeed 5\n"
                +"overwriteAnimeType2 6\noverwriteAnimeSpeed2 2\n");
            GameUiAssets.Config data=GameUiAssets.readConfig(temp.toFile(),"datafile",codec);
            check(Arrays.equals(GameSlotsPanel.CursorAnime.cursorCandidates(data,1,true,true),
                new String[]{"sel_mode1","cur_over","cur_save"}),
                "selectPrintMode 1 prioritizes fileNameSelectPic before cursor sprites");
            check(Arrays.equals(GameSlotsPanel.CursorAnime.cursorCandidates(data,2,true,true),
                new String[]{"","cur_over","cur_save"})&&
                Arrays.equals(GameSlotsPanel.CursorAnime.cursorCandidates(data,2,true,false),
                new String[]{"","","cur_save"})&&
                Arrays.equals(GameSlotsPanel.CursorAnime.cursorCandidates(data,2,false,true),
                new String[]{"","","cur_load"}),
                "overwrite cursor precedes save cursor fallback while load uses load cursor");
            check(data.imageNames().contains("sys/cur_over"),
                "fileNameOverwriteCursor is discovered for GameUiAssets preloading");

            GameSlotsPanel.CursorAnime saveHover=GameSlotsPanel.CursorAnime.read(data,true,false,false);
            GameSlotsPanel.CursorAnime saveClick=GameSlotsPanel.CursorAnime.read(data,true,false,true);
            check(saveHover.pattern==3&&saveHover.type==1&&saveHover.speed==4&&
                saveClick.pattern==3&&saveClick.type==1&&saveClick.speed==4,
                "saveAnime*2 inherits saveAnime* defaults when omitted");
            GameSlotsPanel.CursorAnime overHover=GameSlotsPanel.CursorAnime.read(data,true,true,false);
            GameSlotsPanel.CursorAnime overClick=GameSlotsPanel.CursorAnime.read(data,true,true,true);
            check(overHover.pattern==4&&overHover.type==7&&overHover.speed==5&&
                overClick.pattern==4&&overClick.type==6&&overClick.speed==2,
                "overwriteAnime*2 overrides type/speed while inheriting overwriteAnimePattern");
        }finally{
            deleteTree(temp);
        }

        check(Arrays.equals(new GameSlotsPanel.CursorAnime(1,5,4).frame(9),new int[]{0,0,100,0}),
            "pattern <= 1 stays static and inactive");
        GameSlotsPanel.CursorAnime loop=new GameSlotsPanel.CursorAnime(3,1,4);
        check(Arrays.equals(loop.frame(0),new int[]{0,1,100,1})&&
            Arrays.equals(loop.frame(5),new int[]{1,2,100,1})&&
            Arrays.equals(loop.frame(12),new int[]{0,1,100,1}),
            "type 1 loops vertical cursor frames");
        GameSlotsPanel.CursorAnime lim=new GameSlotsPanel.CursorAnime(3,2,4);
        check(Arrays.equals(lim.frame(4),new int[]{1,2,100,1})&&
            Arrays.equals(lim.frame(12),new int[]{2,2,100,0}),
            "type 2 holds final frame and clears active flag after one cycle");
        GameSlotsPanel.CursorAnime pingPongLim=new GameSlotsPanel.CursorAnime(3,4,2);
        check(Arrays.equals(pingPongLim.frame(4),new int[]{2,1,100,1})&&
            Arrays.equals(pingPongLim.frame(6),new int[]{1,0,100,1})&&
            Arrays.equals(pingPongLim.frame(8),new int[]{0,0,100,0}),
            "type 4 reverses back to frame 0 and stops");
        GameSlotsPanel.CursorAnime blendLoop=new GameSlotsPanel.CursorAnime(2,5,10);
        check(Arrays.equals(blendLoop.frame(3),new int[]{0,1,70,1})&&
            Arrays.equals(blendLoop.frame(15),new int[]{1,0,50,1}),
            "type 5 computes crossfade percent across loop wrap");
        GameSlotsPanel.CursorAnime blendLim=new GameSlotsPanel.CursorAnime(2,6,10);
        check(Arrays.equals(blendLim.frame(5),new int[]{0,1,50,1})&&
            Arrays.equals(blendLim.frame(20),new int[]{1,1,100,0}),
            "type 6 holds last frame cleanly at completion without wrapping to frame 0");
        GameSlotsPanel.CursorAnime blendPingPong=new GameSlotsPanel.CursorAnime(3,7,4);
        check(Arrays.equals(blendPingPong.frame(9),new int[]{2,1,75,1}),
            "type 7 blends reverse ping-pong frames");
    }
    private static void checkInstalledRoot(Path cgListPath,List<String> entries)throws Exception{
        int[][] counts=GalleryCatalog.blockVariantCounts(entries,new int[0],0);
        int sum=0,multi=0;
        for(int[] pair:counts){
            check(pair[0]==0&&pair[1]>=1,"locked installed block count");
            sum+=pair[1];
            if(pair[1]>=2)multi++;
        }
        check(sum==entries.size()&&counts.length>0&&multi>0,
            "installed DeadEndAegis CG blocks partition all variants with multi-variant groups");
        Path parent=cgListPath.toAbsolutePath().getParent();
        Path root=parent!=null?parent.getParent():null;
        if(root!=null&&Files.isRegularFile(root.resolve("init/selectcg.xtx"))&&
            Files.isRegularFile(root.resolve("init/datafile.xtx"))){
            File dir=root.toFile();
            GameUiAssets.Config selectcg=GameUiAssets.readConfig(dir,"selectcg");
            check(GalleryCatalog.differenceFont(selectcg).equals("sys/ta_font_05")&&
                selectcg.imageNames().contains("sys/ta_font_05"),
                "DeadEndAegis selectcg.xtx preloads sys/ta_font_05");
            GameUiAssets.Config datafile=GameUiAssets.readConfig(dir,"datafile");
            GameSlotsPanel.CursorAnime hover=GameSlotsPanel.CursorAnime.read(datafile,true,true,false);
            GameSlotsPanel.CursorAnime click=GameSlotsPanel.CursorAnime.read(datafile,true,true,true);
            check(hover.pattern==2&&hover.type==5&&hover.speed==60&&
                click.pattern==2&&click.type==6&&click.speed==10&&
                GameSlotsPanel.CursorAnime.cursorCandidates(datafile,2,true,true)[1].equals("ta_data_select01"),
                "DeadEndAegis datafile.xtx overwrite cursor and anime parameters");
        }
    }
    public static void main(String[] args)throws Exception{
        List<String> fields=GalleryCatalog.cgTokens("#42-4/cg_anim,*@!#46,3/[<2:90_5|3:75_8]cg_diff\r\nother");
        check(fields.equals(Arrays.asList("#42-4/cg_anim","*@!#46,3/[<2:90_5|3:75_8]cg_diff","other")),
            "ordinary delimiters and an unambiguous animation comma");
        GalleryCatalog.CgVariant cg=GalleryCatalog.cgVariant(fields.get(1),1);
        check(cg.name.equals("cg_diff")&&cg.gore&&cg.difference&&cg.start==46&&cg.frames==3&&cg.unlockIndex==1,
            "animation metadata belongs to the original difference row");
        check(cg.unlocked(new int[]{0x10000001},0)&&!cg.unlocked(new int[]{0x10000000},0),
            "three animation frames use one original unlock key");
        check(cg.x.direction==0&&cg.x.tiles==2&&cg.x.time==90&&cg.x.wait==5&&
            cg.y.direction==1&&cg.y.tiles==3&&cg.y.time==75&&cg.y.wait==8,
            "directional tile counts, scroll duration and delay");
        check(cg.animationPicture(false,"_gore",2).equals("bg\\bganime48")&&
            cg.animationPicture(true,"_gore",2).equals("bg\\bganime_gore48"),
            "animation sprite name follows original gore suffix placement");
        check(cg.picture(true,"_gore",1).equals("cg/cg_diff_gore_2"),"bare original CG family and tile suffix");
        GalleryCatalog.CgVariant explicit=GalleryCatalog.cgVariant("ev\\folder\\ev018.DWQ",2);
        check(explicit.picture(false,"",1).equals("ev/folder/ev018_2"),"explicit CG directory is preserved");
        String trailBackslash=new String(new byte[]{(byte)0x83,0x5c},StandardCharsets.ISO_8859_1);
        GalleryCatalog.CgVariant cp932=GalleryCatalog.cgVariant("ev"+trailBackslash,3);
        check(cp932.picture(false,"",0).equals("ev/ev"+trailBackslash),"CP932 trail is not an explicit path separator");
        check(cg.frameAt(1,2,60)==0&&cg.frameAt(2,2,60)==1&&cg.frameAt(4,2,60)==2&&cg.frameAt(6,2,60)==0,
            "engine frame hold and looping");
        check(cg.frameAt(2,2,2)==2&&cg.frameAt(4,2,2)==2&&cg.frameAt(0,2,1)==2,
            "last allocation receives the last decoded animation frame");
        GalleryCatalog.CgVariant invalid=GalleryCatalog.cgVariant("#2147483647-2/picture",9);
        check(invalid.frames==0&&invalid.frameAt(0,1,60)==-1&&invalid.name.equals("picture"),
            "invalid frame ranges retain a static picture");
        String sjis=new String("@[↑]ev008d".getBytes(Charset.forName("windows-31j")),StandardCharsets.ISO_8859_1);
        GalleryCatalog.CgVariant marked=GalleryCatalog.cgVariant(sjis,15);
        check(marked.difference&&marked.y.direction==0&&marked.y.tiles==1&&marked.name.equals("ev008d"),
            "original CP932 arrow metadata");
        check(marked.picture(false,"",0).equals("ev/ev008d"),"original event CG family");
        checkDifferenceCatalogAndFont();
        for(String path:args){
            Path cgList=Paths.get(path);
            List<String> entries=GalleryCatalog.cgTokens(new String(Files.readAllBytes(cgList),StandardCharsets.ISO_8859_1));
            int scrolls=0;
            for(int i=0;i<entries.size();i++){
                GalleryCatalog.CgVariant entry=GalleryCatalog.cgVariant(entries.get(i),i);
                check(entry.unlockIndex==i&&entry.frames==0&&!entry.name.isEmpty(),"installed gallery row "+i);
                if(entry.x.direction>=0||entry.y.direction>=0)scrolls++;
            }
            check(entries.size()==526&&scrolls==33,"original DeadEndAegis list and scroll marker count");
            checkInstalledRoot(cgList,entries);
        }
        System.out.println("PASS "+checks+" gallery catalog checks");
    }
}
