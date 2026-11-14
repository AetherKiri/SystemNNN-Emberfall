package com.tyranor.runtime.systemnnn;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.Gravity;
import android.widget.TextView;
import java.util.HashMap;
import java.util.Map;
import java.util.function.IntConsumer;

/** The game's CODECERROR artwork and inherited not/can/exit button group. */
final class GameCodecErrorPanel extends GameUiPanel {
    static final int MODE=32;
    private static final String[] TAGS={"notButton","canButton","exitButton"};
    private final GameUiAssets.ModeAssets data;
    final GameRunner.MovieError error;
    private final Paint paint=new Paint(Paint.FILTER_BITMAP_FLAG);

    GameCodecErrorPanel(Context context,GameUiAssets assets,GameRunner.MovieError error,IntConsumer choose){
        super(context,assets,null,()->{});
        this.error=error;data=error.data;
        canvas(assets.sharedConfig.screenWidth,assets.sharedConfig.screenHeight);
        GameUiAssets.Config cfg=data.config;
        boolean artwork=sprite(cfg.value("filenameBG","CodecError"))!=null;
        if(!artwork)put(text("影片无法播放，可跳过影片继续游戏。",24,Color.WHITE),
            canvasWidth/8,canvasHeight/4,canvasWidth*3/4,60);
        if(cfg.number("dialogFlag",1)!=0){
            String name=cfg.value("filenameDialog","ta_dialog5");
            Bitmap dialog=sprite(name);
            if(dialog!=null){
                int dx=cfg.number("dialogPrintX",-999),dy=cfg.number("dialogPrintY",-999);
                if(dx==-999){dx=(canvasWidth-dialog.getWidth())/2;dy=(canvasHeight-dialog.getHeight())/2;}
                picture(name,dx,dy,dialog.getWidth(),dialog.getHeight());
            }
        }
        String[] labels={"跳过影片","播放可用部分","结束游戏"};
        ButtonArt.Setup previous=null;
        int x=(canvasWidth-240)/2,y=canvasHeight/2;
        for(int i=0;i<TAGS.length;i++){
            String key=TAGS[i];
            ButtonArt.Setup setup=new ButtonArt.Setup(cfg,key,previous,false);
            Bitmap image=sprite(setup.art.image);
            if(image==null){
                Map<String,String> fields=new HashMap<>(cfg.fields());
                fields.put((key+"PrintMode").toLowerCase(java.util.Locale.ROOT),"0");
                if(previous==null){
                    fields.putIfAbsent((key+"SizeX").toLowerCase(java.util.Locale.ROOT),"240");
                    fields.putIfAbsent((key+"SizeY").toLowerCase(java.util.Locale.ROOT),"48");
                }
                setup=new ButtonArt.Setup(cfg.withValues(fields),key,previous,false);
            }
            x=cfg.number(key+"PrintX",x);y=cfg.number(key+"PrintY",i==0||image!=null?y:y+60);
            GameButton.Spec spec=new GameButton.Spec(setup);
            int action=i;
            boolean enabled=i!=1||error.capability!=0;
            if(image!=null){
                GameButton button=animatedButton(spec,image,labels[i],()->sound(spec.sound,spec.voice),
                    ()->choose.accept(action),x,y,setup.width,setup.height);
                ButtonArt.Setup feedback=setup;
                button.feedback(event->sound(feedback.sound(event),feedback.voice(event)),setup.enterWait,setup.exitWait);
                button.setEnabled(enabled);button.setClickable(enabled);
            }else{
                TextView button=text(labels[i],24,enabled?Color.WHITE:Color.GRAY);
                button.setGravity(Gravity.CENTER);button.setBackgroundColor(0xff263238);
                button.setContentDescription(labels[i]);button.setFocusable(true);
                button.setEnabled(enabled);
                if(enabled)button.setOnClickListener(v->{sound(spec.sound,spec.voice);choose.accept(action);});
                put(button,x,y,setup.width,setup.height);
            }
            previous=setup;
        }
        opening(cfg,false);
        uiBackground(cfg,MODE,0);
    }
    @Override Bitmap sprite(String name){
        if(name==null)return null;
        Bitmap found=data==null?null:data.images.get(GameUiAssets.systemImageKey(name));
        return found==null?super.sprite(name):found;
    }
    @Override void drawPanelBackground(Canvas canvas){
        GameUiAssets.Config cfg=data.config;
        if(cfg.number("backScriptFlag",0)!=0)return;
        Bitmap background=sprite(cfg.value("filenameBG","CodecError"));
        int save=canvas.save();
        canvas.scale(getWidth()/(float)canvasWidth,getHeight()/(float)canvasHeight);
        if(background!=null)canvas.drawBitmap(background,0,0,paint);
        canvas.restoreToCount(save);
    }
}
