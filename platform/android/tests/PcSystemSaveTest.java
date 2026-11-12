package com.tyranor.runtime.systemnnn;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Locale;
import java.util.stream.Stream;

/** Round-trips a supplied Windows system save or installation template in a temporary sav directory. */
public final class PcSystemSaveTest {
    public static void main(String[] args) throws Exception {
        File root=new File(args[0]);
        Path sample=Paths.get(args[1]);
        int stride=Integer.parseInt(args[2]);
        boolean template=sample.toString().endsWith(".org");
        boolean extendedFixture=args.length>3 && args[3].equals("extended");
        String name=extendedFixture?"sysfile2.sav":sample.getFileName().toString();
        byte[] original=Files.readAllBytes(sample);
        if(extendedFixture){
            original=Arrays.copyOf(original,record(original,17));
            words(original).putInt(32,14);
        }
        Path directory=Files.createTempDirectory("pc-system-test-");
        try{
            Path live=directory.resolve(name.replace(".org",".sav"));
            Path input=directory.resolve(name);
            Files.write(input,original);
            GameUiAssets.Config game=GameUiAssets.readConfig(root,"game");
            String archiveName=name.toLowerCase(Locale.ROOT);
            int version=archiveName.contains("file3.")?3:archiveName.contains("file2.")?2:1;
            PcSystemSave save=PcSystemSave.open(root,directory.toFile(),game,null,
                new int[]{version,archiveName.startsWith("t")?1:0});
            PcSystemSave.Initial initial=save.load();
            check(initial.live!=template,"live archive selection");
            GameProgress.State imported=save.restore(initial,blank());
            int[] gallery=save.restoreGallery(initial,new int[0]);
            if(!template){
                check(PcSystemSave.valid(live.toFile()),"archive accepted for backup");
                int variable=words(original).getInt(record(original,6)+32+9*4);
                check(imported.variables[9]==variable,"system variable read");
                for(int id:imported.read){
                    int bit=(id>>>16)*stride+(id&0xffff);
                    check(set(original,5,bit),"message bit read");
                }
                int erased=1<<28|7777;
                check(!has(save.restoreGallery(initial,new int[]{erased}),erased),"old Android unlock did not override PC");
            }

            int message=1<<16|123;
            int cg=1<<28|700, film=2<<28|1<<16|127, song=3<<28|200, movie=4<<28|4, choice=5<<28|17<<16|7;
            int[] keys=append(gallery,cg,film,song,movie,choice);
            imported.variables[9]=41;
            imported.variables[999]=-17;
            imported.results[1024+4]=90;
            imported.optional[6]=3;
            imported.optional[1024+1]=16;
            imported.text[2]=new byte[]{'A'};
            GameProgress.State edited=new GameProgress.State(append(imported.read,message),imported.variables,
                imported.results,imported.text,imported.optional,null,Collections.emptyList());
            save.write(edited,keys);
            byte[] written=Files.readAllBytes(live);
            check(written.length==original.length,"archive extent preserved");
            check(set(written,5,stride+123),"PC message bit written");
            check(set(written,7,700)&&set(written,9,stride/2+127),"PC CG and film bits written");
            check(set(written,11,5)&&set(written,11,4)==set(original,11,4),"PC movie uses one-based numbering");
            check(words(written).getInt(record(written,12)+32+200*4)==1,"PC music unlock counter written");
            check(words(written).getInt(record(written,6)+32+9*4)==41,"PC route variable written");
            check(Arrays.equals(slice(original,8),slice(written,8)),"untouched scene record preserved");
            check(Arrays.equals(Arrays.copyOfRange(slice(original,1),96,224),Arrays.copyOfRange(slice(written,1),96,224)),
                "PC audio and text settings preserved");
            if(template)check(Arrays.equals(Files.readAllBytes(input),original),"factory template unchanged");

            PcSystemSave.Initial reopened=save.load();
            GameProgress.State loaded=save.restore(reopened,null);
            int[] restored=save.restoreGallery(reopened,new int[0]);
            check(has(loaded.read,message)&&has(restored,cg)&&has(restored,film)&&has(restored,song)&&has(restored,movie),
                "progress survived reopening");
            if(words(original).getInt(record(original,6))==4128)
                check(loaded.variables[999]==-17,"expanded system variables");
            if(record(written,18)>=0)check(has(restored,choice)&&set(written,18,17*32+7),"PC selected choice written");
            if(record(written,14)>=0)check(loaded.results[1024+4]==90&&loaded.optional[6]==3&&
                loaded.optional[1024+1]==16&&loaded.text[2][0]=='A',"PC achievements, term, voice and replacement text");
            save.write(loaded,restored);
            check(Arrays.equals(Files.readAllBytes(live),written),"unchanged progress did not rewrite");
            Files.write(live,Arrays.copyOf(written,17));
            check(!PcSystemSave.valid(live.toFile()),"truncated archive rejected");
            check(!save.load().live,"truncated archive left out of startup progress");
            save.write(edited,keys);
            check(Files.size(live)==17,"damaged PC archive preserved");
            chartProgress(directory);
            System.out.println("PASS "+root.getName()+" "+name+" stride="+stride+" read="+imported.read.length+
                " gallery="+gallery.length+(extendedFixture?" (source-layout fixture)":""));
        }finally{
            try(Stream<Path> files=Files.walk(directory)){
                for(Path file:(Iterable<Path>)files.sorted(Comparator.reverseOrder())::iterator)Files.delete(file);
            }
        }
    }

    private static void chartProgress(Path directory) throws Exception {
        Path root=Files.createDirectory(directory.resolve("chart-game"));
        Path init=Files.createDirectory(root.resolve("init"));
        Path spt=Files.createDirectory(root.resolve("spt"));
        Path sav=Files.createDirectory(root.resolve("sav"));
        Files.write(spt.resolve("var2.fxf"),new byte[0]);
        Files.writeString(init.resolve("game.xtx"),"SelectChartIsDefault 1\n");
        Files.writeString(init.resolve("selectChart.xtx"),"chartBlockSize 5\n");
        Files.write(sav.resolve("mita.dat"),new byte[]{0,1,0,0,0,0,0,0});
        Files.write(sav.resolve("tsuka.dat"),new byte[]{0,2,0,0,0});
        GameUiAssets.Config game=GameUiAssets.readConfig(root.toFile(),"game");
        GameUiAssets.Config chart=GameUiAssets.readConfig(root.toFile(),"selectChart");
        PcSystemSave save=PcSystemSave.open(root.toFile(),sav.toFile(),game,chart,new int[]{3,0});
        GameProgress.State state=save.restore(save.load(),blank());
        check(state.chart[9]==1&&state.chart[17]==2,"PC chart files restored at their own offsets");
        state.chart[10]=3;state.chart[18]=4;
        save.write(state,new int[0]);
        check(Files.isRegularFile(sav.resolve("sysfile3.sav"))&&!Files.exists(sav.resolve("sysfile2.sav")),
            "the cached format selects the first system archive");
        check(Files.readAllBytes(sav.resolve("mita.dat"))[2]==3&&Files.readAllBytes(sav.resolve("tsuka.dat"))[2]==4,
            "chart progress was written in PC form");
        Files.write(sav.resolve("mita.dat"),new byte[]{7});
        save.write(state,new int[0]);
        check(Files.size(sav.resolve("mita.dat"))==1,"invalid PC chart file was retained");
    }

    private static GameProgress.State blank(){
        byte[][] text=new byte[102][0];
        return new GameProgress.State(new int[0],new int[1000],new int[2048],text,
            new int[NativeGame.OPTIONAL_PROGRESS_WORDS],null,Collections.emptyList());
    }
    private static ByteBuffer words(byte[] bytes){return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);}
    private static int record(byte[] bytes,int code){
        ByteBuffer data=words(bytes);
        for(int offset=0;offset<bytes.length;offset+=data.getInt(offset))if(data.getInt(offset+4)==code)return offset;
        return -1;
    }
    private static byte[] slice(byte[] bytes,int code){
        int offset=record(bytes,code);
        return Arrays.copyOfRange(bytes,offset,offset+words(bytes).getInt(offset));
    }
    private static boolean set(byte[] bytes,int code,int bit){
        return (bytes[record(bytes,code)+32+bit/8]&(1<<(bit%8)))!=0;
    }
    private static int[] append(int[] first,int... extra){
        int[] result=Arrays.copyOf(first,first.length+extra.length);
        System.arraycopy(extra,0,result,first.length,extra.length);
        return result;
    }
    private static boolean has(int[] values,int key){for(int value:values)if(value==key)return true;return false;}
    private static void check(boolean condition,String subject){if(!condition)throw new AssertionError(subject);}
}
