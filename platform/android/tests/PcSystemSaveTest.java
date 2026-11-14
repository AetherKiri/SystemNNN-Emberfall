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
        if(args.length==0){chartLayouts();System.out.println("PASS PC chart layouts and progress wire");return;}
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
            int[] audio={100,87,200,0,500,-4,506,3};
            int[] owners={6,1};
            check(save.writeAudioSettings(audio,owners),"write original audio settings");
            PcSystemSave.Initial audioReloaded=save.load();
            for(int i=0;i<audio.length;i+=2)check(pair(audioReloaded.audioParameters(),audio[i])==audio[i+1],
                "audio parameter "+audio[i]+" survives PC archive reload");
            check(pair(audioReloaded.audioOwners(),6)==1,"original character mute flag survives reload");
            byte[] withAudio=Files.readAllBytes(live);
            for(int block:new int[]{5,6,7,8,9,10,11,12})
                check(Arrays.equals(slice(written,block),slice(withAudio,block)),"audio edit preserved progress block "+block);
            if(version==3){
                check(save.writeAudioSettings(new int[]{109,83,209,1},new int[]{0,4}),"write PC version 3 audio settings");
                audioReloaded=save.load();
                check(audioReloaded.audioTheme()==4&&pair(audioReloaded.audioParameters(),109)==83&&
                    pair(audioReloaded.audioParameters(),209)==1,"version 3 theme and script SE survive reload");
            }
            int[] nonAudio={2,1,4,0,6,1,105,2,106,35,107,75,300,2,315,1,400,1,415,0};
            check(save.writeSystemSettings(nonAudio,new int[]{1,2},3,1),"write non-audio PC system settings");
            PcSystemSave.Initial sysReloaded=save.load();
            for(int i=0;i<nonAudio.length;i+=2)check(pair(sysReloaded.audioParameters(),nonAudio[i])==nonAudio[i+1],
                "non-audio parameter "+nonAudio[i]+" survives PC archive reload");
            check(pair(sysReloaded.audioOwners(),1)==2&&sysReloaded.messageCursor()==3&&sysReloaded.mouseCursor()==1,
                "messageWindow owner, messageCursor and mouseCursor survive PC archive reload");
            if(version==3){
                int[] flag2Checks={210,1,211,0,212,1,213,1,214,0};
                check(save.writeSystemSettings(flag2Checks,new int[0],Integer.MIN_VALUE,Integer.MIN_VALUE),
                    "write PC version 3 SYSTEMFLAG2 check flags");
                PcSystemSave.Initial flag2Reloaded=save.load();
                for(int i=0;i<flag2Checks.length;i+=2)
                    check(pair(flag2Reloaded.audioParameters(),flag2Checks[i])==flag2Checks[i+1],
                        "SYSTEMFLAG2 check "+flag2Checks[i]+" survives PC archive reload");
                check(flag2Reloaded.messageCursor()==3&&flag2Reloaded.mouseCursor()==1,
                    "MIN_VALUE cursor sentinels preserve existing messageCursor and mouseCursor");
            }
            PcSystemSave restarted=PcSystemSave.open(root,directory.toFile(),game,null,
                new int[]{version,archiveName.startsWith("t")?1:0});
            check(pair(restarted.load().audioParameters(),200)==0,"BGM mute survives a new session");
            check(restarted.writeAudioSettings(new int[]{200,1},new int[0]),"write the unmuted BGM setting");
            check(pair(save.load().audioParameters(),200)==1,"BGM unmute survives another load");
            Files.write(live,Arrays.copyOf(written,17));
            check(!PcSystemSave.valid(live.toFile()),"truncated archive rejected");
            check(!save.load().live,"truncated archive left out of startup progress");
            check(!save.writeAudioSettings(audio,owners),"damaged PC archive rejected for audio edits");
            save.write(edited,keys);
            check(Files.size(live)==17,"damaged PC archive preserved");
            System.out.println("PASS "+root.getName()+" "+name+" stride="+stride+" read="+imported.read.length+
                " gallery="+gallery.length+(extendedFixture?" (source-layout fixture)":""));
        }finally{
            try(Stream<Path> files=Files.walk(directory)){
                for(Path file:(Iterable<Path>)files.sorted(Comparator.reverseOrder())::iterator)Files.delete(file);
            }
        }
    }

    static void chartLayouts() throws Exception {
        check(PcSystemSave.isChartFile("MITA.DAT")&&PcSystemSave.isChartFile("TSUKA.DAT"),"PC chart names accept ASCII case");
        for(String name:new String[]{"m\u0131ta.dat","t\u017fuka.dat","tsu\u212aa.dat"})
            check(!PcSystemSave.isChartFile(name),"Unmanaged Unicode name is not a PC chart alias");
        Path directory=Files.createTempDirectory("pc-chart-test-");
        try{
            chartProgress(directory);
            legacyChartProgress(directory);
            chartWireProgress(directory);
            chartSymbolicDirectory(directory);
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
        Path legacy=Files.createDirectory(sav.resolve("mushiex"));
        byte[] otherSeen=new byte[]{41},otherRoads=new byte[]{42};
        Files.write(legacy.resolve("mita.dat"),otherSeen);Files.write(legacy.resolve("tsuka.dat"),otherRoads);
        GameUiAssets.Config game=GameUiAssets.readConfig(root.toFile(),"game");
        GameUiAssets.Config chart=GameUiAssets.readConfig(root.toFile(),"selectChart");
        PcSystemSave save=PcSystemSave.open(root.toFile(),sav.toFile(),game,chart,new int[]{3,0});
        GameProgress.State state=save.restore(save.load(),blank());
        check(words(state.chart).getInt()==8&&words(state.chart).getInt(4)==5,"modern chart keeps its padded wire extent");
        check(state.chart[9]==1&&state.chart[17]==2,"PC chart files restored at their own offsets");
        state.chart[10]=3;state.chart[18]=4;
        save.write(state,new int[0]);
        check(Files.isRegularFile(sav.resolve("sysfile3.sav"))&&!Files.exists(sav.resolve("sysfile2.sav")),
            "the cached format selects the first system archive");
        check(Files.readAllBytes(sav.resolve("mita.dat"))[2]==3&&Files.readAllBytes(sav.resolve("tsuka.dat"))[2]==4,
            "chart progress was written in PC form");
        check(Arrays.equals(save.restore(save.load(),blank()).chart,state.chart),"modern chart progress survives reload");
        check(Arrays.equals(Files.readAllBytes(legacy.resolve("mita.dat")),otherSeen)&&
            Arrays.equals(Files.readAllBytes(legacy.resolve("tsuka.dat")),otherRoads),"modern chart never writes the legacy directory");
        byte[] seenBefore=Files.readAllBytes(sav.resolve("mita.dat")),roadsBefore=Files.readAllBytes(sav.resolve("tsuka.dat"));
        save.write(withChart(chartFlags(10004,10000)),new int[0]);
        check(Arrays.equals(Files.readAllBytes(sav.resolve("mita.dat")),seenBefore)&&
            Arrays.equals(Files.readAllBytes(sav.resolve("tsuka.dat")),roadsBefore),"modern writer rejects a legacy wire payload");
        Files.write(sav.resolve("mita.dat"),new byte[]{7});
        save.write(state,new int[0]);
        check(Files.size(sav.resolve("mita.dat"))==1,"invalid PC chart file was retained");
    }

    private static void legacyChartProgress(Path directory) throws Exception {
        Path root=Files.createDirectory(directory.resolve("legacy-chart-game"));
        Path init=Files.createDirectory(root.resolve("init")),sav=Files.createDirectory(root.resolve("sav"));
        Files.writeString(init.resolve("game.xtx"),"SelectChartClassDisable 1\n");
        Files.writeString(init.resolve("chart.xtx"),"chartListNumber 9\nchartBlockSize 1000\n");
        byte[] otherSeen=new byte[]{31,32},otherRoads=new byte[]{33,34};
        Files.write(sav.resolve("mita.dat"),otherSeen);Files.write(sav.resolve("tsuka.dat"),otherRoads);
        GameUiAssets.Config game=GameUiAssets.readConfig(root.toFile(),"game"),chart=GameUiAssets.readConfig(root.toFile(),"chart");
        PcSystemSave save=PcSystemSave.open(root.toFile(),sav.toFile(),game,chart,new int[]{3,0},true);
        byte[] flags=chartFlags(10004,10000);
        flags[8+1001]=5;flags[8+10004+9999]=6;
        byte[] tail={17,34,51,68};System.arraycopy(tail,0,flags,8+10000,tail.length);
        check(!Files.exists(sav.resolve("mushiex")),"a new legacy directory starts absent");
        save.write(withChart(flags),new int[0]);
        Path legacy=sav.resolve("mushiex"),seen=legacy.resolve("mita.dat"),roads=legacy.resolve("tsuka.dat");
        check(Files.size(seen)==10004&&Files.size(roads)==10000,"legacy chart keeps its fixed PC extents");
        check(Arrays.equals(Arrays.copyOfRange(Files.readAllBytes(seen),10000,10004),tail),"all four legacy tail bytes survive first write");
        check(Arrays.equals(Files.readAllBytes(sav.resolve("mita.dat")),otherSeen)&&
            Arrays.equals(Files.readAllBytes(sav.resolve("tsuka.dat")),otherRoads),"legacy chart never overwrites modern chart files");
        check(!Files.exists(sav.resolve("emberfall.chart")),"legacy progress does not create a second chart tree");
        PcSystemSave reopened=PcSystemSave.open(root.toFile(),sav.toFile(),game,chart,new int[]{3,0},true);
        byte[] stale=chartFlags(10004,10000);Arrays.fill(stale,8,stale.length,(byte)99);
        GameProgress.State restored=reopened.restore(reopened.load(),withChart(stale));
        check(Arrays.equals(restored.chart,flags),"PC legacy files replace stale Android flags without cropping the tail");
        flags[8+10000]=85;reopened.write(withChart(flags),new int[0]);
        check(Arrays.equals(reopened.restore(reopened.load(),blank()).chart,flags),"legacy tail edits survive another read/write cycle");
        PcSystemSave unrecognized=PcSystemSave.open(root.toFile(),sav.toFile(),game,chart,new int[]{3,0},false);
        check(unrecognized.restore(unrecognized.load(),blank()).chart==null,"chart filenames alone do not opt an unknown game into the legacy layout");
        byte[] beforeSeen=Files.readAllBytes(seen),beforeRoads=Files.readAllBytes(roads);
        for(byte[] unknown:new byte[][]{chartFlags(10000,10000),chartFlags(10008,9996)}){
            reopened.write(withChart(unknown),new int[0]);
            check(Arrays.equals(Files.readAllBytes(seen),beforeSeen)&&Arrays.equals(Files.readAllBytes(roads),beforeRoads),
                "legacy writer ignores modern or unknown wire extents");
        }
        Files.write(seen,Arrays.copyOf(beforeSeen,10000));
        reopened.write(withChart(flags),new int[0]);
        check(Files.size(seen)==10000,"a truncated legacy PC file is preserved, not expanded from Android state");
    }

    private static void chartWireProgress(Path directory) throws Exception {
        Path base=directory.resolve("emberfall.read");GameProgress store=new GameProgress(base.toFile());
        for(byte[] flags:new byte[][]{chartFlags(8,5),chartFlags(10000,10000),chartFlags(10004,10000)}){
            flags[flags.length-1]=73;if(flags.length==20012)Arrays.fill(flags,10008,10012,(byte)91);
            store.write(withChart(flags));
            check(Arrays.equals(store.read().chart,flags),"known chart wire layout round-trips unchanged");
        }
        Path extension=directory.resolve("emberfall.read.ext");
        byte[] beforeBase=Files.readAllBytes(base),beforeExtension=Files.readAllBytes(extension);
        for(byte[] unknown:new byte[][]{chartFlags(10008,9996),chartFlags(10004,9999),chartFlags(9,5),chartFlags(0,0)}){
            boolean rejected=false;try{store.write(withChart(unknown));}catch(java.io.IOException expected){rejected=true;}
            check(rejected,"unknown chart wire layout rejected on write");
            check(Arrays.equals(Files.readAllBytes(base),beforeBase)&&Arrays.equals(Files.readAllBytes(extension),beforeExtension),
                "rejected chart write preserves the committed progress generation");
        }
        byte[] corrupted=beforeExtension.clone();int payload=corrupted.length-20012;
        words(corrupted).putInt(payload,10008).putInt(payload+4,9996);
        Files.write(extension,corrupted);
        check(store.read().chart==null,"unknown chart shape is rejected when reading an otherwise valid extension");
        check(Arrays.equals(Files.readAllBytes(base),beforeBase),"invalid extension does not damage base progress");
    }

    private static void chartSymbolicDirectory(Path directory) throws Exception {
        Path root=Files.createDirectory(directory.resolve("linked-chart-game"));
        Path init=Files.createDirectory(root.resolve("init")),sav=Files.createDirectory(root.resolve("sav"));
        Files.write(init.resolve("chart.xtx"),new byte[0]);
        Path outside=Files.createDirectory(directory.resolve("external-chart")),link=sav.resolve("mushiex");
        byte[] seen=new byte[10004],roads=new byte[10000];Arrays.fill(seen,(byte)19);Arrays.fill(roads,(byte)23);
        Files.write(outside.resolve("mita.dat"),seen);Files.write(outside.resolve("tsuka.dat"),roads);
        try{Files.createSymbolicLink(link,outside.toAbsolutePath());}
        catch(java.io.IOException|UnsupportedOperationException|SecurityException unavailable){
            System.out.println("SKIP chart symbolic-directory checks: symbolic links unavailable");return;
        }
        GameUiAssets.Config game=GameUiAssets.readConfig(root.toFile(),"game"),chart=GameUiAssets.readConfig(root.toFile(),"chart");
        boolean rejected=false;
        try{PcSystemSave.open(root.toFile(),sav.toFile(),game,chart,new int[]{3,0},true);}
        catch(java.io.IOException expected){rejected=true;}
        check(rejected,"legacy chart open rejects an external symbolic directory");
        Files.delete(link);
        PcSystemSave save=PcSystemSave.open(root.toFile(),sav.toFile(),game,chart,new int[]{3,0},true);
        Files.createSymbolicLink(link,outside.toAbsolutePath());
        rejected=false;
        try{save.write(withChart(chartFlags(10004,10000)),new int[0]);}
        catch(java.io.IOException expected){rejected=true;}
        check(rejected,"legacy chart write rejects a symbolic directory installed after open");
        check(Arrays.equals(Files.readAllBytes(outside.resolve("mita.dat")),seen)&&
            Arrays.equals(Files.readAllBytes(outside.resolve("tsuka.dat")),roads),"rejected chart operations leave external flags untouched");
    }

    private static byte[] chartFlags(int seen,int roads){
        return ByteBuffer.allocate(8+seen+roads).order(ByteOrder.LITTLE_ENDIAN).putInt(seen).putInt(roads).array();
    }
    private static GameProgress.State withChart(byte[] chart){
        GameProgress.State base=blank();
        return new GameProgress.State(base.read,base.variables,base.results,base.text,base.optional,chart,base.legacy);
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
    private static int pair(int[] values,int id){
        for(int i=0;i<values.length;i+=2)if(values[i]==id)return values[i+1];
        throw new AssertionError("Missing parameter "+id);
    }
    private static void check(boolean condition,String subject){if(!condition)throw new AssertionError(subject);}
}
